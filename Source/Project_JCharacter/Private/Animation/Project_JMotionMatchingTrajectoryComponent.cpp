// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"

#include "Animation/Project_JMotionMatchingCVars.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionTrajectoryLibrary.h"
#include "Project_JPlayerCharacter.h"
#include "Async/ParallelFor.h"

namespace
{
int32 FindPresentTrajectorySampleIndex(const FTransformTrajectory& Trajectory)
{
	int32 PresentSampleIndex = INDEX_NONE;
	float SmallestAbsTime = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
	{
		const float AbsTime = FMath::Abs(Trajectory.Samples[Index].TimeInSeconds);
		if (AbsTime < SmallestAbsTime)
		{
			SmallestAbsTime = AbsTime;
			PresentSampleIndex = Index;
		}
	}

	return PresentSampleIndex;
}

FVector GetHorizontalSampleDelta(const FTransformTrajectory& Trajectory, int32 FromIndex, int32 ToIndex)
{
	if (!Trajectory.Samples.IsValidIndex(FromIndex) || !Trajectory.Samples.IsValidIndex(ToIndex))
	{
		return FVector::ZeroVector;
	}

	FVector Delta = Trajectory.Samples[ToIndex].GetTransform().GetLocation() - Trajectory.Samples[FromIndex].GetTransform().GetLocation();
	Delta.Z = 0.0f;
	return Delta;
}

FVector ResolveTrajectorySampleDirection(const FTransformTrajectory& Trajectory, int32 SampleIndex, const FVector& FallbackDirection)
{
	FVector Direction = GetHorizontalSampleDelta(Trajectory, SampleIndex, SampleIndex + 1);
	if (Direction.IsNearlyZero())
	{
		Direction = GetHorizontalSampleDelta(Trajectory, SampleIndex - 1, SampleIndex);
	}

	return Direction.IsNearlyZero() ? FallbackDirection : Direction.GetSafeNormal();
}

bool ShouldRepairRemoteTrajectoryFacingForOwner(const ACharacter& CharacterOwner, FVector& OutVelocityDirection)
{
	if (const AProject_JPlayerCharacter* PlayerOwner = Cast<AProject_JPlayerCharacter>(&CharacterOwner);
		PlayerOwner && !PlayerOwner->AllowsStraightRunningTrajectoryRepair())
	{
		return false;
	}

	FVector HorizontalVelocity(CharacterOwner.GetVelocity().X, CharacterOwner.GetVelocity().Y, 0.0f);
	const float GroundSpeed = HorizontalVelocity.Size();
	if (GroundSpeed < Project_J::MotionMatchingCVars::GetRepairRemoteTrajectoryFacingMinSpeed())
	{
		return false;
	}

	OutVelocityDirection = HorizontalVelocity / GroundSpeed;
	const float VelocityYaw = OutVelocityDirection.Rotation().Yaw;
	const float ActorVelocityYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(CharacterOwner.GetActorRotation().Yaw, VelocityYaw));
	if (ActorVelocityYawDelta > Project_J::MotionMatchingCVars::GetRepairRemoteTrajectoryFacingMaxYawDelta())
	{
		// The actor is still visually turning; Arc/Prism candidates are allowed to win in this window.
		return false;
	}

	return true;
}
}

UProject_JMotionMatchingTrajectoryComponent::UProject_JMotionMatchingTrajectoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JMotionMatchingTrajectoryComponent::InitializeComponent()
{
	Super::InitializeComponent();
	EnsureTrajectoryBuffers();
}

void UProject_JMotionMatchingTrajectoryComponent::ResetTrajectoryHistory()
{
	EnsureTrajectoryBuffers();

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		Trajectory.Samples.Reset();
		TranslationHistory.Reset();
		LastUpdateFrameNumber = 0;
		return;
	}

	SamplingData.Init();
	CharacterTrajectoryData.UpdateDataFromCharacter(0.0f, CharacterOwner);
	Trajectory.Samples.Reset();
	TranslationHistory.Reset();

	FMotionTrajectoryLibrary::InitTrajectorySamples(
		Trajectory,
		SamplingData,
		CharacterOwner->GetActorLocation(),
		CharacterOwner->GetActorQuat());

	TranslationHistory.SetNumZeroed(SamplingData.NumHistorySamples);
	LastUpdateFrameNumber = GFrameCounter;

	PreviousFilteredTrajectory = Trajectory;
	if (CharacterOwner && CharacterOwner->GetCharacterMovement())
	{
		LastMaxWalkSpeed = CharacterOwner->GetCharacterMovement()->MaxWalkSpeed;
	}
	else
	{
		LastMaxWalkSpeed = 0.0f;
	}
}

void UProject_JMotionMatchingTrajectoryComponent::EnsureTrajectoryBuffers()
{
	SamplingData.Init();

	const int32 RequiredTrajectorySamples = SamplingData.NumHistorySamples + 1 + SamplingData.NumPredictionSamples;
	Trajectory.Samples.Reserve(RequiredTrajectorySamples);
	TranslationHistory.Reserve(SamplingData.NumHistorySamples);
}

void UProject_JMotionMatchingTrajectoryComponent::UpdateTrajectoryState(float DeltaTime)
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	// 0. Update the actual trajectory samples manually since component tick is disabled
	if (IsRegistered())
	{
		Super::TickComponent(DeltaTime, ELevelTick::LEVELTICK_All, nullptr);
	}

	// 1. Speed change correction (applies to all characters, local or simulated)
	if (bEnableSpeedChangeCorrection)
	{
		if (const UCharacterMovementComponent* MoveComp = CharacterOwner->GetCharacterMovement())
		{
			float CurrentMaxWalkSpeed = MoveComp->MaxWalkSpeed;
			if (LastMaxWalkSpeed > 0.0f && !FMath::IsNearlyEqual(CurrentMaxWalkSpeed, LastMaxWalkSpeed))
			{
				float SpeedScaleRatio = CurrentMaxWalkSpeed / LastMaxWalkSpeed;
				ScaleTrajectoryHistory(SpeedScaleRatio);
			}
			LastMaxWalkSpeed = CurrentMaxWalkSpeed;
		}
	}

	const bool bSimulatedProxy = CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;

	// 2. Trajectory smoothing (only for simulated proxies)
	// Keep remote smoothing opt-in. Network smoothing can make positions look correct while delaying
	// trajectory sample rotations, which Pose Search interprets as an arc/turn query.
	if (bEnableTrajectorySmoothing &&
		bSimulatedProxy &&
		(Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryPosition() ||
			Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryRotation()))
	{
		ApplyTrajectorySmoothing(DeltaTime);
	}
	else if (bSimulatedProxy)
	{
		PreviousFilteredTrajectory = Trajectory;
	}

	if (bSimulatedProxy &&
		Project_J::MotionMatchingCVars::ShouldRepairRemoteTrajectoryFacing())
	{
		RepairRemoteTrajectoryFacing(*CharacterOwner);
	}
}

void UProject_JMotionMatchingTrajectoryComponent::ScaleTrajectoryHistory(float ScaleRatio)
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner || Trajectory.Samples.IsEmpty() || ScaleRatio <= 0.0f)
	{
		return;
	}

	FTransform ActorTransform = CharacterOwner->GetActorTransform();

	for (FTransformTrajectorySample& Sample : Trajectory.Samples)
	{
		// Only scale history samples (TimeInSeconds < 0)
		if (Sample.TimeInSeconds < -UE_KINDA_SMALL_NUMBER)
		{
			// Convert to local space
			FTransform LocalTransform = Sample.GetTransform().GetRelativeTransform(ActorTransform);

			// Scale the local position offset
			FVector LocalPosition = LocalTransform.GetLocation();
			LocalPosition.X *= ScaleRatio;
			LocalPosition.Y *= ScaleRatio;
			LocalTransform.SetLocation(LocalPosition);

			// Convert back to world space
			Sample.SetTransform(LocalTransform * ActorTransform);
		}
	}

	// Update the cache as well so we don't snap back in the next smoothing step
	PreviousFilteredTrajectory = Trajectory;
}

void UProject_JMotionMatchingTrajectoryComponent::ApplyTrajectorySmoothing(float DeltaTime)
{
	if (Trajectory.Samples.IsEmpty() || DeltaTime <= 0.0f)
	{
		return;
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	// Initialize previous filtered samples if size mismatch or first time
	if (PreviousFilteredTrajectory.Samples.Num() != Trajectory.Samples.Num())
	{
		PreviousFilteredTrajectory = Trajectory;
		return;
	}

	FTransform ActorTransform = CharacterOwner->GetActorTransform();
	float Alpha = FMath::Clamp(DeltaTime * TrajectorySmoothingSpeed, 0.0f, 1.0f);
	// Remote smoothing is CVar-gated because local-space filtering can bend straight replicated history into arcs.
	const bool bSmoothPosition =
		CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy ||
		Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryPosition();
	const bool bSmoothRotation =
		CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy ||
		Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryRotation();

	const int32 NumSamples = Trajectory.Samples.Num();
	constexpr int32 ParallelSmoothingSampleThreshold = 64;

	auto SmoothingLogic = [&](int32 i)
	{
		FTransformTrajectorySample& CurrentSample = Trajectory.Samples[i];
		const FTransformTrajectorySample& PrevSample = PreviousFilteredTrajectory.Samples[i];

		// Convert both current and previous samples to local space relative to the CURRENT actor transform.
		// This guarantees that constant-speed movement has zero lag.
		FTransform LocalCurrent = CurrentSample.GetTransform().GetRelativeTransform(ActorTransform);
		FTransform LocalPrev = PrevSample.GetTransform().GetRelativeTransform(ActorTransform);

		// Interpolate in local space
		const FVector LocalPos = bSmoothPosition
			? FMath::Lerp(LocalPrev.GetLocation(), LocalCurrent.GetLocation(), Alpha)
			: LocalCurrent.GetLocation();
		const FQuat LocalRot = bSmoothRotation
			? FQuat::FastLerp(LocalPrev.GetRotation(), LocalCurrent.GetRotation(), Alpha).GetNormalized()
			: LocalCurrent.GetRotation();

		FTransform LocalSmoothed(LocalRot, LocalPos, FVector::OneVector);

		// Convert back to world space
		CurrentSample.SetTransform(LocalSmoothed * ActorTransform);
	};

	if (NumSamples >= ParallelSmoothingSampleThreshold)
	{
		ParallelFor(NumSamples, SmoothingLogic);
	}
	else
	{
		for (int32 i = 0; i < NumSamples; ++i)
		{
			SmoothingLogic(i);
		}
	}

	PreviousFilteredTrajectory = Trajectory;
}

void UProject_JMotionMatchingTrajectoryComponent::RepairRemoteTrajectoryFacing(const ACharacter& CharacterOwner)
{
	// Simulated proxies do not have the same local input/control signal as the owning client.
	// After a replicated turn, their trajectory positions may already be straight while future
	// sample rotations still describe the previous arc. Pose Search uses those rotations, not
	// only sample positions, so straight remote running can otherwise keep selecting Prism/Arc.
	FVector VelocityDirection = FVector::ZeroVector;
	if (!ShouldRepairRemoteTrajectoryFacingForOwner(CharacterOwner, VelocityDirection))
	{
		return;
	}

	const int32 NumSamples = Trajectory.Samples.Num();
	if (NumSamples < 2)
	{
		return;
	}

	const int32 PresentSampleIndex = FindPresentTrajectorySampleIndex(Trajectory);
	if (PresentSampleIndex == INDEX_NONE)
	{
		return;
	}

	const FTransform PresentTransform = Trajectory.Samples[PresentSampleIndex].GetTransform();
	const FVector PresentDirectionSafe = ResolveTrajectorySampleDirection(Trajectory, PresentSampleIndex, VelocityDirection);
	const float PresentMoveYaw = PresentDirectionSafe.Rotation().Yaw;
	const float PresentFaceYaw = PresentTransform.GetRotation().Rotator().Yaw;
	// Preserve the project's trajectory facing convention instead of forcing facing to velocity.
	// For the current run schema, healthy straight-running samples have a stable offset near -90 deg.
	const float FacingOffsetYaw = FMath::FindDeltaAngleDegrees(PresentMoveYaw, PresentFaceYaw);

	FVector LastValidDirection = PresentDirectionSafe;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		// Keep historical rotations intact; they describe the turn that just happened.
		// Only repair current/predicted facing when the remote actor is already moving straight again.
		if (Trajectory.Samples[Index].TimeInSeconds < -UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FTransform CurrentTransform = Trajectory.Samples[Index].GetTransform();
		LastValidDirection = ResolveTrajectorySampleDirection(Trajectory, Index, LastValidDirection);

		if (LastValidDirection.IsNearlyZero())
		{
			continue;
		}

		FTransform RepairedTransform = CurrentTransform;
		const float RepairedFacingYaw = LastValidDirection.Rotation().Yaw + FacingOffsetYaw;
		RepairedTransform.SetRotation(FRotator(0.0f, RepairedFacingYaw, 0.0f).Quaternion());
		Trajectory.Samples[Index].SetTransform(RepairedTransform);
	}

	PreviousFilteredTrajectory = Trajectory;
}
