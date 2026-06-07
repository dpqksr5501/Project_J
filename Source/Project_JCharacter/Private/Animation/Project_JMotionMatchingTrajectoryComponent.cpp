// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionTrajectoryLibrary.h"

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

	// 2. Trajectory smoothing (only for simulated proxies)
	if (bEnableTrajectorySmoothing && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		ApplyTrajectorySmoothing(DeltaTime);
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

	for (int32 i = 0; i < Trajectory.Samples.Num(); ++i)
	{
		FTransformTrajectorySample& CurrentSample = Trajectory.Samples[i];
		const FTransformTrajectorySample& PrevSample = PreviousFilteredTrajectory.Samples[i];

		// Convert both current and previous samples to local space relative to the CURRENT actor transform.
		// This guarantees that constant-speed movement has zero lag.
		FTransform LocalCurrent = CurrentSample.GetTransform().GetRelativeTransform(ActorTransform);
		FTransform LocalPrev = PrevSample.GetTransform().GetRelativeTransform(ActorTransform);

		// Interpolate in local space
		FVector LocalPos = FMath::Lerp(LocalPrev.GetLocation(), LocalCurrent.GetLocation(), Alpha);
		FQuat LocalRot = FQuat::FastLerp(LocalPrev.GetRotation(), LocalCurrent.GetRotation(), Alpha).GetNormalized();

		FTransform LocalSmoothed(LocalRot, LocalPos, FVector::OneVector);

		// Convert back to world space
		CurrentSample.SetTransform(LocalSmoothed * ActorTransform);
	}

	PreviousFilteredTrajectory = Trajectory;
}
