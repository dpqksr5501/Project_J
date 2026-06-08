// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility_Sprint.generated.h"

/**
 * Local Predicted Sprint Ability
 * 클라이언트에서 즉각적으로 실행되어(Input Lag 방지) 캐릭터에게 Sprint 태그를 부여하고,
 * 서버에서 이를 승인(Authority)하여 원격 프록시에 복제합니다.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_Sprint : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UProject_JGameplayAbility_Sprint();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;

protected:
	/** 어빌리티 활성화 중 부여할 Gameplay Effect (Sprint 상태 태그 포함) */
	UPROPERTY(EditDefaultsOnly, Category = "Sprint")
	TSubclassOf<class UGameplayEffect> SprintEffectClass;

private:
	FActiveGameplayEffectHandle ActiveSprintEffectHandle;
};
