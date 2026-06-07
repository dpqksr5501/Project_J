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

	// Grant Abilities
	for (int32 AbilityIndex = 0; AbilityIndex < GrantedGameplayAbilities.Num(); ++AbilityIndex)
	{
		const TSubclassOf<UGameplayAbility>& AbilityToGrant = GrantedGameplayAbilities[AbilityIndex];
		if (AbilityToGrant)
		{
			UGameplayAbility* AbilityCDO = AbilityToGrant->GetDefaultObject<UGameplayAbility>();
			FGameplayAbilitySpec AbilitySpec(AbilityCDO, 1, INDEX_NONE, SourceObject);

			const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);
			OutGrantedHandles->AbilitySpecHandles.Add(AbilitySpecHandle);
		}
	}

	// Grant Effects
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
