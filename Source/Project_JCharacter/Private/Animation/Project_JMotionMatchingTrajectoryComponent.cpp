// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"

#include "Animation/Project_JMotionMatchingCVars.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "MotionTrajectoryLibrary.h"
#include "Project_JPlayerCharacter.h"

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

	// UCharacterTrajectoryComponent is an example implementation that generates
	// unconditionally from OnCharacterMovementUpdated. Project_J generates from
	// the player presentation pipeline after movement policy/rotation are applied,
	// so remove the example binding and retain only its protected data model.
	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		CharacterOwner->OnCharacterMovementUpdated.RemoveAll(this);
	}

	EnsureTrajectoryBuffers();
}

void UProject_JMotionMatchingTrajectoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// The parent example allocates its sample buffers in BeginPlay. A dedicated
	// server never consumes animation trajectory, so release that persistent
	// per-player storage after base lifecycle initialization completes.
	if (GetNetMode() == NM_DedicatedServer)
	{
		Trajectory.Samples.Empty();
		TranslationHistory.Empty();
		PreviousFilteredTrajectory.Samples.Empty();
		InvalidateSamplingIndexCache();
	}
}

void UProject_JMotionMatchingTrajectoryComponent::ResetTrajectoryHistory()

{
	ResetTrajectoryHistoryWithReason(EProject_JTrajectoryResetReason::Manual);
}

void UProject_JMotionMatchingTrajectoryComponent::ResetTrajectoryHistoryWithReason(EProject_JTrajectoryResetReason Reason)
{
	EnsureTrajectoryBuffers();
	InvalidateSamplingIndexCache();
	LastResetReason = Reason;
	++ResetRevision;
	LastGenerationFrameCounter = TNumericLimits<uint64>::Max();
	LastPostProcessFrameCounter = TNumericLimits<uint64>::Max();

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		Trajectory.Samples.Reset();
		TranslationHistory.Reset();
		LastUpdateFrameNumber = 0;
		bWasTrajectoryGenerationEligible = false;
		return;
	}

	SamplingData.Init();
	Trajectory.Samples.Reset();
	TranslationHistory.Reset();
	const USkeletalMeshComponent* MeshComponent = CharacterOwner->GetMesh();
	const FVector InitialPosition = MeshComponent ? MeshComponent->GetComponentLocation() : CharacterOwner->GetActorLocation();
	const FQuat InitialFacing = MeshComponent ? MeshComponent->GetComponentQuat() : CharacterOwner->GetActorQuat();

	FMotionTrajectoryLibrary::InitTrajectorySamples(
		Trajectory,
		SamplingData,
		InitialPosition,
		InitialFacing);

	TranslationHistory.SetNumZeroed(SamplingData.NumHistorySamples);
	LastUpdateFrameNumber = 0;

	PreviousFilteredTrajectory = Trajectory;
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
	if (!CharacterOwner || DeltaTime <= 0.0f)
	{
		return;
	}

	const bool bShouldGenerate = ShouldGenerateTrajectory(*CharacterOwner);
	if (!bShouldGenerate)
	{
		bWasTrajectoryGenerationEligible = false;
		return;
	}

	if (!bWasTrajectoryGenerationEligible)
	{
		ResetTrajectoryHistoryWithReason(
			bHasGeneratedTrajectory
				? EProject_JTrajectoryResetReason::PresentationWake
				: EProject_JTrajectoryResetReason::Initialization);
		bWasTrajectoryGenerationEligible = true;
	}

	if (LastGenerationFrameCounter != GFrameCounter)
	{
		GenerateTrajectory(*CharacterOwner, DeltaTime);
	}
	PostProcessTrajectory(*CharacterOwner, DeltaTime);
}

float UProject_JMotionMatchingTrajectoryComponent::GetTrajectoryAgeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World || LastGeneratedWorldTimeSeconds < 0.0)
	{
		return -1.0f;
	}

	return static_cast<float>(FMath::Max(World->GetTimeSeconds() - LastGeneratedWorldTimeSeconds, 0.0));
}
bool UProject_JMotionMatchingTrajectoryComponent::ShouldGenerateTrajectory(const ACharacter& CharacterOwner) const
{
	if (CharacterOwner.GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	if (const AProject_JPlayerCharacter* PlayerOwner = Cast<AProject_JPlayerCharacter>(&CharacterOwner))
	{
		const UProject_JMountComponent* MountComponent = PlayerOwner->GetMountComponent();
		if (MountComponent && MountComponent->IsMounted())
		{
			return false;
		}
	}

	if (CharacterOwner.IsLocallyControlled())
	{
		return true;
	}

	const USkeletalMeshComponent* MeshComponent = CharacterOwner.GetMesh();
	return MeshComponent && MeshComponent->WasRecentlyRendered(RemoteRecentlyRenderedTolerance);
}

void UProject_JMotionMatchingTrajectoryComponent::GenerateTrajectory(ACharacter& CharacterOwner, float DeltaTime)
{
	EnsureTrajectoryBuffers();
	const int32 RequiredTrajectorySamples = SamplingData.NumHistorySamples + 1 + SamplingData.NumPredictionSamples;
	if (Trajectory.Samples.Num() != RequiredTrajectorySamples ||
		TranslationHistory.Num() != SamplingData.NumHistorySamples)
	{
		ResetTrajectoryHistoryWithReason(EProject_JTrajectoryResetReason::Initialization);
		bWasTrajectoryGenerationEligible = true;
	}

	CharacterTrajectoryData.UpdateDataFromCharacter(DeltaTime, &CharacterOwner);
	FMotionTrajectoryLibrary::UpdateHistory_TransformHistory(
		Trajectory,
		TranslationHistory,
		CharacterTrajectoryData,
		SamplingData,
		DeltaTime);
	FMotionTrajectoryLibrary::UpdatePrediction_SimulateCharacterMovement(
		Trajectory,
		CharacterTrajectoryData,
		SamplingData);

	LastGenerationFrameCounter = GFrameCounter;
	LastUpdateFrameNumber = GFrameNumber;
	LastGeneratedWorldTimeSeconds = CharacterOwner.GetWorld()
		? CharacterOwner.GetWorld()->GetTimeSeconds()
		: -1.0;
	bHasGeneratedTrajectory = true;
	++GenerationRevision;
}

void UProject_JMotionMatchingTrajectoryComponent::PostProcessTrajectory(ACharacter& CharacterOwner, float DeltaTime)
{
	if (LastPostProcessFrameCounter == GFrameCounter || LastGenerationFrameCounter != GFrameCounter)
	{
		return;
	}
	LastPostProcessFrameCounter = GFrameCounter;

	const bool bSimulatedProxy = CharacterOwner.GetLocalRole() == ROLE_SimulatedProxy;

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
		RepairRemoteTrajectoryFacing(CharacterOwner);
	}
}

bool UProject_JMotionMatchingTrajectoryComponent::TryGetFuturePlanarVelocity(
	float PredictionHorizon,
	const FVector& CurrentPlanarVelocity,
	FVector& OutVelocity,
	float& OutTurnAngleDegrees) const
{
	OutVelocity = FVector::ZeroVector;
	OutTurnAngleDegrees = 0.0f;
	RefreshSamplingIndexCache(PredictionHorizon);

	if (!Trajectory.Samples.IsValidIndex(CachedPresentSampleIndex) ||
		!Trajectory.Samples.IsValidIndex(CachedFutureSampleIndex))
	{
		return false;
	}

	const FTransformTrajectorySample& PresentSample = Trajectory.Samples[CachedPresentSampleIndex];
	const FTransformTrajectorySample& FutureSample = Trajectory.Samples[CachedFutureSampleIndex];
	const float SampleDeltaTime = FutureSample.TimeInSeconds - PresentSample.TimeInSeconds;
	if (SampleDeltaTime <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutVelocity =
		(FutureSample.GetTransform().GetLocation() - PresentSample.GetTransform().GetLocation()) /
		SampleDeltaTime;
	OutVelocity.Z = 0.0f;

	FVector HorizontalVelocity = CurrentPlanarVelocity;
	HorizontalVelocity.Z = 0.0f;
	if (!HorizontalVelocity.IsNearlyZero() && !OutVelocity.IsNearlyZero())
	{
		const float DirectionDot = FMath::Clamp(
			FVector::DotProduct(HorizontalVelocity.GetSafeNormal2D(), OutVelocity.GetSafeNormal2D()),
			-1.0f,
			1.0f);
		OutTurnAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
	}

	return true;
}

void UProject_JMotionMatchingTrajectoryComponent::InvalidateSamplingIndexCache()
{
	CachedPresentSampleIndex = INDEX_NONE;
	CachedFutureSampleIndex = INDEX_NONE;
	CachedTrajectorySampleCount = INDEX_NONE;
	CachedPredictionHorizon = -1.0f;
}

void UProject_JMotionMatchingTrajectoryComponent::RefreshSamplingIndexCache(float PredictionHorizon) const
{
	const float SafePredictionHorizon = FMath::Max(PredictionHorizon, 0.0f);
	if (CachedTrajectorySampleCount == Trajectory.Samples.Num() &&
		FMath::IsNearlyEqual(CachedPredictionHorizon, SafePredictionHorizon) &&
		Trajectory.Samples.IsValidIndex(CachedPresentSampleIndex) &&
		Trajectory.Samples.IsValidIndex(CachedFutureSampleIndex))
	{
		return;
	}

	CachedPresentSampleIndex = INDEX_NONE;
	CachedFutureSampleIndex = INDEX_NONE;
	CachedTrajectorySampleCount = Trajectory.Samples.Num();
	CachedPredictionHorizon = SafePredictionHorizon;

	float BestPresentTime = TNumericLimits<float>::Max();
	float BestFutureTimeDelta = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
	{
		const FTransformTrajectorySample& Sample = Trajectory.Samples[Index];
		const float AbsoluteSampleTime = FMath::Abs(Sample.TimeInSeconds);
		if (AbsoluteSampleTime < BestPresentTime)
		{
			BestPresentTime = AbsoluteSampleTime;
			CachedPresentSampleIndex = Index;
		}

		if (Sample.TimeInSeconds > UE_KINDA_SMALL_NUMBER)
		{
			const float HorizonDelta = FMath::Abs(Sample.TimeInSeconds - SafePredictionHorizon);
			if (HorizonDelta < BestFutureTimeDelta)
			{
				BestFutureTimeDelta = HorizonDelta;
				CachedFutureSampleIndex = Index;
			}
		}
	}
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

	for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
	{
		FTransformTrajectorySample& CurrentSample = Trajectory.Samples[Index];
		const FTransformTrajectorySample& PrevSample = PreviousFilteredTrajectory.Samples[Index];

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
