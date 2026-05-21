// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimationModifier.generated.h"

USTRUCT(BlueprintType)
struct FProject_JLocomotionNotifyRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	EProject_JLocomotionAnimEvent EventType = EProject_JLocomotionAnimEvent::GroundStartFinished;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Time = 0.0f;
};

UCLASS(BlueprintType, Blueprintable)
class PROJECT_JCHARACTEREDITOR_API UProject_JLocomotionAnimationModifier : public UAnimationModifier
{
	GENERATED_BODY()

public:
	virtual void OnApply_Implementation(UAnimSequence* AnimationSequence) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FName NotifyTrackName = TEXT("ProjectJ_Locomotion");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bClearExistingProjectJTrack = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	TArray<FProject_JLocomotionNotifyRule> NotifyRules;
};
