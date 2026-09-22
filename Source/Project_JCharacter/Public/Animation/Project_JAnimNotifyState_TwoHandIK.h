// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Project_JAnimNotifyState_TwoHandIK.generated.h"

/**
 * AnimNotifyState that activates Two-Hand IK grip during an attack swing, skill, or interaction window.
 *
 * Placed on attack montages to pull the secondary hand (usually left hand) to the weapon's secondary
 * grip socket ('WeaponGrip_L') during the authored swing interval without requiring keyframed float curves.
 */
UCLASS(meta = (DisplayName = "Two-Hand Grip IK"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_TwoHandIK : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	/** Target alpha for secondary (usually left) hand IK during this state interval. Default 1.0 (fully grips handle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Two-Hand IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SecondaryIKAlpha = 1.0f;

	/** Target alpha for primary (usually right) hand IK during this state interval. Default 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Two-Hand IK", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverridePrimaryIK"))
	float PrimaryIKAlpha = 1.0f;

	/** If true, also overrides primary hand IK alpha during this notify. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Two-Hand IK")
	bool bOverridePrimaryIK = false;
};
