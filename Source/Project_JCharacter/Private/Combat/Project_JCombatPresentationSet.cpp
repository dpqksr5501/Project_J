#include "Combat/Project_JCombatPresentationSet.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "NiagaraSystem.h"
#include "NiagaraEffectType.h"
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
		if (!Cue.CueTag.IsValid() || Seen.HasTagExact(Cue.CueTag) || Cue.NiagaraSystem.IsNull())
		{
			Context.AddError(FText::FromString(TEXT("Combat VFX cues require unique valid tags and a Niagara system."))); bValid = false;
		}
		else if (const auto* System = Cue.NiagaraSystem.LoadSynchronous(); !IsValid(System))
		{
			// This is an explicit editor validation boundary, never a frame/cue
			// execution path. Retain the hard property's existence/type guarantee.
			Context.AddError(FText::FromString(FString::Printf(TEXT("Combat VFX system is missing or is not Niagara: %s"),
				*Cue.NiagaraSystem.ToSoftObjectPath().ToString()))); bValid = false;
		}
		else if (!Cue.bLooping)
		{
			// Scalability resume keeps the component registered after culling. A
			// one-shot has no tracked Niagara component for a matching notify or
			// attack end to retire, even when a scalar cue lease exists.
			// Reaction is EffectType-wide in UE5.8; current-platform culling being
			// disabled does not make this lifetime safe on another platform/quality.
			const auto* Type = System->GetEffectType();
			if (Type && (Type->CullReaction == ENiagaraCullReaction::DeactivateResume ||
				Type->CullReaction == ENiagaraCullReaction::DeactivateImmediateResume ||
				Type->CullReaction == ENiagaraCullReaction::PauseResume))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Combat VFX cue '%s' has no tracked notify-owned Niagara lifetime (bLooping=false), but system '%s' uses resumable EffectType '%s'. One-shots require a non-resuming policy."),
					*Cue.CueTag.ToString(), *System->GetPathName(), *Type->GetPathName())));
				bValid = false;
			}
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
