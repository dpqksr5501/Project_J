// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility_CombatToggle.generated.h"

/**
 * Local Predicted Combat Toggle Ability
 * 전투/비전투 모드를 전환합니다. 클라이언트에서 즉각 반응하며, 서버에서 승인/복제합니다.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_CombatToggle : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UProject_JGameplayAbility_CombatToggle();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:
	/** 전투 모드를 부여할 때 사용하는 Gameplay Effect */
	UPROPERTY(EditDefaultsOnly, Category = "Combat Mode")
	TSubclassOf<class UGameplayEffect> CombatModeEffectClass;
};
