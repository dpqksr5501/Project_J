#include "AbilitySystem/Project_JAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JAbilitySystemComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Validation/Project_JDataValidation.h"
#endif

void UProject_JAbilitySet::GiveToAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject, const FName GrantSourceId) const
{
	check(ASC);

	if (!OutGrantedHandles)
	{
		return;
	}

	UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	if (!GrantSourceId.IsNone())
	{
		if (!ProjectJASC || !ProjectJASC->ReserveAbilityGrantSource(GrantSourceId))
		{
			return;
		}
		OutGrantedHandles->GrantSourceIds.Add(GrantSourceId);
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
		AbilitySpec.GetDynamicSpecSourceTags().AppendTags(AbilityEntry.AdditionalInputTags);

		const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);
		OutGrantedHandles->AbilitySpecHandles.Add(AbilitySpecHandle);
		if (ProjectJASC && !GrantSourceId.IsNone())
		{
			ProjectJASC->RegisterGrantedAbility(GrantSourceId, AbilitySpecHandle);
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
		if (ProjectJASC && !GrantSourceId.IsNone())
		{
			ProjectJASC->RegisterGrantedEffect(GrantSourceId, GameplayEffectHandle);
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

	if (!GrantedHandles->GrantSourceIds.IsEmpty())
	{
		if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
		{
			for (const FName GrantSourceId : GrantedHandles->GrantSourceIds)
			{
				ProjectJASC->RemoveAbilityGrantSource(GrantSourceId);
			}
			GrantedHandles->AbilitySpecHandles.Reset();
			GrantedHandles->GameplayEffectHandles.Reset();
			GrantedHandles->GrantSourceIds.Reset();
			return;
		}
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
	GrantedHandles->GrantSourceIds.Reset();
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

		FGameplayTagContainer EntryInputTags = AbilityEntry.AdditionalInputTags;
		if (AbilityEntry.InputTag.IsValid())
		{
			EntryInputTags.AddTag(AbilityEntry.InputTag);
		}
		for (const FGameplayTag& EntryInputTag : EntryInputTags)
		{
			if (!EntryInputTag.IsValid())
			{
				continue;
			}
			if (SeenInputTags.Contains(EntryInputTag))
			{
				Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
					NSLOCTEXT("ProjectJAbilitySet", "DuplicateInputTag", "InputTag '{0}' is assigned to more than one ability entry."),
					FText::FromString(EntryInputTag.ToString())));
			}
			SeenInputTags.Add(EntryInputTag);
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

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
