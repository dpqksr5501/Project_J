#include "AbilitySystem/Project_JAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

void UProject_JAbilitySet::GiveToAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject) const
{
	check(ASC);

	if (!OutGrantedHandles)
	{
		return;
	}

	for (const FProject_JAbilitySet_GameplayAbility& AbilityEntry : GrantedAbilityEntries)
	{
		if (!AbilityEntry.Ability)
		{
			continue;
		}

		FGameplayAbilitySpec AbilitySpec(AbilityEntry.Ability, FMath::Max(1, AbilityEntry.AbilityLevel), AbilityEntry.InputID, SourceObject);
		if (AbilityEntry.InputTag.IsValid())
		{
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityEntry.InputTag);
		}

		const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);
		OutGrantedHandles->AbilitySpecHandles.Add(AbilitySpecHandle);
	}

	// Backward-compatible legacy grants.
	for (int32 AbilityIndex = 0; AbilityIndex < GrantedGameplayAbilities.Num(); ++AbilityIndex)
	{
		const TSubclassOf<UGameplayAbility>& AbilityToGrant = GrantedGameplayAbilities[AbilityIndex];
		if (AbilityToGrant)
		{
			FGameplayAbilitySpec AbilitySpec(AbilityToGrant, 1, INDEX_NONE, SourceObject);

			const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);
			OutGrantedHandles->AbilitySpecHandles.Add(AbilitySpecHandle);
		}
	}

	for (const FProject_JAbilitySet_GameplayEffect& EffectEntry : GrantedEffectEntries)
	{
		if (!EffectEntry.Effect)
		{
			continue;
		}

		const UGameplayEffect* GameplayEffect = EffectEntry.Effect->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle GameplayEffectHandle = ASC->ApplyGameplayEffectToSelf(GameplayEffect, FMath::Max(1.0f, EffectEntry.EffectLevel), ASC->MakeEffectContext());
		OutGrantedHandles->GameplayEffectHandles.Add(GameplayEffectHandle);
	}

	// Backward-compatible legacy effects.
	for (int32 EffectIndex = 0; EffectIndex < GrantedGameplayEffects.Num(); ++EffectIndex)
	{
		const TSubclassOf<UGameplayEffect>& EffectToGrant = GrantedGameplayEffects[EffectIndex];
		if (EffectToGrant)
		{
			const UGameplayEffect* GameplayEffect = EffectToGrant->GetDefaultObject<UGameplayEffect>();
			const FActiveGameplayEffectHandle GameplayEffectHandle = ASC->ApplyGameplayEffectToSelf(GameplayEffect, 1.0f, ASC->MakeEffectContext());
			OutGrantedHandles->GameplayEffectHandles.Add(GameplayEffectHandle);
		}
	}
}

void UProject_JAbilitySet::TakeFromAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* GrantedHandles) const
{
	check(ASC);

	if (!GrantedHandles)
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : GrantedHandles->AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}

	for (const FActiveGameplayEffectHandle& Handle : GrantedHandles->GameplayEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}

	GrantedHandles->AbilitySpecHandles.Reset();
	GrantedHandles->GameplayEffectHandles.Reset();
}
