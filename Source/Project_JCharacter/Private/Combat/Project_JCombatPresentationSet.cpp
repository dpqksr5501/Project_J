#include "Combat/Project_JCombatPresentationSet.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FProject_JCombatVFXCueDefinition* UProject_JAttackPresentationProfile::FindCue(const FGameplayTag CueTag) const
{
	if (!CueTag.IsValid())
	{
		return nullptr;
	}

	for (const FProject_JCombatVFXCueDefinition& Cue : Cues)
	{
		if (Cue.CueTag.MatchesTagExact(CueTag))
		{
			return &Cue;
		}
	}
	return nullptr;
}

const UProject_JAttackPresentationProfile* UProject_JCombatPresentationSet::FindProfile(const FGameplayTag AttackTag) const
{
	if (!AttackTag.IsValid())
	{
		return nullptr;
	}

	for (const FProject_JCombatAttackPresentationEntry& Entry : AttackPresentations)
	{
		if (Entry.AttackTag.MatchesTagExact(AttackTag))
		{
			return Entry.Profile;
		}
	}
	return nullptr;
}

#if WITH_EDITOR
EDataValidationResult UProject_JAttackPresentationProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	bool bValid = Parent != EDataValidationResult::Invalid;
	FGameplayTagContainer Seen;
	for (const auto& Cue : Cues)
	{
		if (!Cue.CueTag.IsValid() || Seen.HasTagExact(Cue.CueTag) || !Cue.NiagaraSystem)
		{
			Context.AddError(FText::FromString(TEXT("Combat VFX cues require unique valid tags and a Niagara system."))); bValid = false;
		}
		Seen.AddTag(Cue.CueTag);
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
EDataValidationResult UProject_JCombatPresentationSet::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	bool bValid = Parent != EDataValidationResult::Invalid;
	FGameplayTagContainer Seen;
	for (const auto& Entry : AttackPresentations)
	{
		if (!Entry.AttackTag.IsValid() || Seen.HasTagExact(Entry.AttackTag) || !Entry.Profile)
		{
			Context.AddError(FText::FromString(TEXT("Combat presentation entries require unique valid attack tags and a profile."))); bValid = false;
		}
		Seen.AddTag(Entry.AttackTag);
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
