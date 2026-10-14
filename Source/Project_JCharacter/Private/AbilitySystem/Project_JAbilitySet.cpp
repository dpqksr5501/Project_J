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

	check(IsInGameThread());
	if (!ASC->IsOwnerActorAuthoritative() || !OutGrantedHandles || OutGrantedHandles->MutationRevision == MAX_uint64)
	{
		return;
	}

	UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	uint64 GrantGeneration = 0;
	const uint64 MutationRevision = OutGrantedHandles->MutationRevision;
	if (!GrantSourceId.IsNone())
	{
		if (OutGrantedHandles->GrantSourceGenerations.Contains(GrantSourceId)) return;
		bool bNeedsGrant = false;
		if (!ProjectJASC || !ProjectJASC->AcquireAbilityGrantSource(GrantSourceId, bNeedsGrant, GrantGeneration))
		{
			return;
		}
		OutGrantedHandles->GrantSourceGenerations.Add(GrantSourceId, GrantGeneration);
		if (!bNeedsGrant) return;
	}
	// Keyed specs/effects have one owner: the ASC registry. A provider keeps only
	// its generation lease. Unkeyed sets keep individual handles instead.
	const auto CanContinue = [&]() {
		return GrantSourceId.IsNone() ? OutGrantedHandles->MutationRevision == MutationRevision :
			ProjectJASC->IsAbilityGrantSourceCurrent(GrantSourceId, GrantGeneration);
	};

	for (const FProject_JAbilitySet_GameplayAbility& AbilityEntry : GrantedAbilityEntries)
	{
		if (!CanContinue()) { return; }
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
		if (!GrantSourceId.IsNone())
		{
			if (!ProjectJASC->RegisterGrantedAbility(GrantSourceId, GrantGeneration, AbilitySpecHandle))
			{ ASC->CancelAbilityHandle(AbilitySpecHandle); ASC->ClearAbility(AbilitySpecHandle); return; }
		}
		else if (CanContinue()) { OutGrantedHandles->AbilitySpecHandles.Add(AbilitySpecHandle); }
		else { ASC->CancelAbilityHandle(AbilitySpecHandle); ASC->ClearAbility(AbilitySpecHandle); return; }
	}

	for (const FProject_JAbilitySet_GameplayEffect& EffectEntry : GrantedEffectEntries)
	{
		if (!CanContinue()) { return; }
		if (!EffectEntry.Effect)
		{
			continue;
		}

		const UGameplayEffect* GameplayEffect = EffectEntry.Effect->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle GameplayEffectHandle = ASC->ApplyGameplayEffectToSelf(GameplayEffect, FMath::Max(1.0f, EffectEntry.EffectLevel), ASC->MakeEffectContext());
		if (!GrantSourceId.IsNone())
		{
			// Instant effects have no removable handle. They are committed once by GAS.
			if (GameplayEffectHandle.IsValid() && !ProjectJASC->RegisterGrantedEffect(GrantSourceId, GrantGeneration, GameplayEffectHandle))
			{ ASC->RemoveActiveGameplayEffect(GameplayEffectHandle); return; }
		}
		else if (CanContinue()) { if (GameplayEffectHandle.IsValid()) OutGrantedHandles->GameplayEffectHandles.Add(GameplayEffectHandle); }
		else { if (GameplayEffectHandle.IsValid()) ASC->RemoveActiveGameplayEffect(GameplayEffectHandle); return; }
	}

}

void UProject_JAbilitySet::TakeFromAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* GrantedHandles) const
{
	check(ASC);

	if (!GrantedHandles)
	{
		return;
	}
	check(IsInGameThread());
	// Detach before any GAS removal delegate. A nested new grant owns the fresh
	// container and cannot be reset by completion of this old release.
	const uint64 NextRevision = GrantedHandles->MutationRevision == MAX_uint64 ? MAX_uint64 : GrantedHandles->MutationRevision + 1;
	FProject_JAbilitySet_GrantedHandles Retired = MoveTemp(*GrantedHandles);
	*GrantedHandles = FProject_JAbilitySet_GrantedHandles();
	GrantedHandles->MutationRevision = NextRevision;
	if (!Retired.GrantSourceGenerations.IsEmpty())
	{
		if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
		{
			for (const auto& Lease : Retired.GrantSourceGenerations)
			{
				ProjectJASC->RemoveAbilityGrantSource(Lease.Key, Lease.Value);
			}
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : Retired.AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}

	for (const FActiveGameplayEffectHandle& Handle : Retired.GameplayEffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}

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
