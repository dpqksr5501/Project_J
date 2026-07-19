// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatAnimationLayerComponent.generated.h"

/**
 * Links the equipped weapon family's Anim Layer into whichever production
 * humanoid master AnimBP is assigned by the owning job Blueprint.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatAnimationLayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatAnimationLayerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Call after equipment, combat, or mount state changes. */
	void RefreshLayer();

private:
	void UnlinkLayer();

	/** Hard reference held only while the weapon layer is actively linked. */
	TSubclassOf<class UAnimInstance> LinkedAnimationLayerClass;
};
