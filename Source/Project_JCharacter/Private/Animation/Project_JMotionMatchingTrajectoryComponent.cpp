// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"

#include "GameFramework/Character.h"
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
}

void UProject_JMotionMatchingTrajectoryComponent::EnsureTrajectoryBuffers()
{
	SamplingData.Init();

	const int32 RequiredTrajectorySamples = SamplingData.NumHistorySamples + 1 + SamplingData.NumPredictionSamples;
	Trajectory.Samples.Reserve(RequiredTrajectorySamples);
	TranslationHistory.Reserve(SamplingData.NumHistorySamples);
}
