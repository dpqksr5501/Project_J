// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Project_JGameplayAbility_CombatToggle.h"
#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

UProject_JGameplayAbility_CombatToggle::UProject_JGameplayAbility_CombatToggle()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// 핵심: 로컬 예측을 통해 버튼을 누르자마자 무기를 꺼냄 (Input Lag 제거)
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(FProject_JGameplayTags::Get().State_CombatMode);
	SetAssetTags(AssetTags);

	// State.CombatMode belongs to the persistent GameplayEffect. Giving it to this
	// short-lived ability makes its first activation look like combat is already active.
}

bool UProject_JGameplayAbility_CombatToggle::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return true;
}

void UProject_JGameplayAbility_CombatToggle::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (ASC && CombatModeEffectClass)
	{
		// 현재 전투 모드인지 태그로 확인
		bool bIsCombatMode = ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);

		if (bIsCombatMode)
		{
			// 전투 모드 해제 (Effect 제거)
			ASC->RemoveActiveGameplayEffectBySourceEffect(CombatModeEffectClass, ASC);
		}
		else
		{
			// 전투 모드 진입 (Effect 부여)
			FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CombatModeEffectClass, GetAbilityLevel());
			if (SpecHandle.IsValid())
			{
				ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
			}
		}
	}

	// 상태만 변경하고 즉시 종료 (토글 방식이므로)
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
