#include "AbilitySystem/Project_JAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Validation/Project_JDataValidation.h"
#endif

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

#if WITH_EDITOR
EDataValidationResult UProject_JAbilitySet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;

	TSet<FGameplayTag> SeenInputTags;
	for (int32 AbilityIndex = 0; AbilityIndex < GrantedAbilityEntries.Num(); ++AbilityIndex)
	{
		const FProject_JAbilitySet_GameplayAbility& AbilityEntry = GrantedAbilityEntries[AbilityIndex];
		if (!AbilityEntry.Ability)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "MissingAbilityEntry", "GrantedAbilityEntries[{0}] has no Ability class."),
				FText::AsNumber(AbilityIndex)));
		}

		if (AbilityEntry.AbilityLevel < 1)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "InvalidAbilityLevel", "GrantedAbilityEntries[{0}] has AbilityLevel below 1."),
				FText::AsNumber(AbilityIndex)));
		}

		if (AbilityEntry.InputTag.IsValid())
		{
			if (SeenInputTags.Contains(AbilityEntry.InputTag))
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
					NSLOCTEXT("ProjectJAbilitySet", "DuplicateInputTag", "InputTag '{0}' is assigned to more than one ability entry."),
					FText::FromString(AbilityEntry.InputTag.ToString())));
			}
			SeenInputTags.Add(AbilityEntry.InputTag);
		}
	}

	for (int32 EffectIndex = 0; EffectIndex < GrantedEffectEntries.Num(); ++EffectIndex)
	{
		const FProject_JAbilitySet_GameplayEffect& EffectEntry = GrantedEffectEntries[EffectIndex];
		if (!EffectEntry.Effect)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "MissingEffectEntry", "GrantedEffectEntries[{0}] has no Effect class."),
				FText::AsNumber(EffectIndex)));
		}

		if (EffectEntry.EffectLevel <= 0.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "InvalidEffectLevel", "GrantedEffectEntries[{0}] has EffectLevel below or equal to 0."),
				FText::AsNumber(EffectIndex)));
		}
	}

	for (int32 AbilityIndex = 0; AbilityIndex < GrantedGameplayAbilities.Num(); ++AbilityIndex)
	{
		if (!GrantedGameplayAbilities[AbilityIndex])
		{
			Project_J::DataValidation::AddWarning(Context, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "MissingLegacyAbility", "GrantedGameplayAbilities[{0}] is empty."),
				FText::AsNumber(AbilityIndex)));
		}
	}

	for (int32 EffectIndex = 0; EffectIndex < GrantedGameplayEffects.Num(); ++EffectIndex)
	{
		if (!GrantedGameplayEffects[EffectIndex])
		{
			Project_J::DataValidation::AddWarning(Context, FText::Format(
				NSLOCTEXT("ProjectJAbilitySet", "MissingLegacyEffect", "GrantedGameplayEffects[{0}] is empty."),
				FText::AsNumber(EffectIndex)));
		}
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
