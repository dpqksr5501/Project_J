#include "Combat/Project_JCombatPresentationSet.h"

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
