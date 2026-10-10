#include "UI/Project_JCharacterUIProfile.h"
#include "AbilitySystemComponent.h"

bool UProject_JCharacterUIProfile::IsUsable() const
{
	if (ProfileId.IsNone() || (bOverrideSkills && Skills.Num() > 64) || (bOverrideResources && Resources.Num() > 6))
		return false;
	TSet<FGameplayTag> Inputs;
	if (bOverrideSkills)
		for (const auto &Slot : Skills)
		{
			if (!Slot.InputTag.IsValid() || Inputs.Contains(Slot.InputTag))
				return false;
			Inputs.Add(Slot.InputTag);
		}
	TSet<FName> IDs;
	if (bOverrideResources)
		for (const auto &Resource : Resources)
		{
			if (Resource.ResourceId.IsNone() || IDs.Contains(Resource.ResourceId) || !Resource.Current.IsValid() ||
				!Resource.Maximum.IsValid())
				return false;
			IDs.Add(Resource.ResourceId);
		}
	return true;
}
bool UProject_JCharacterUIProfile::Matches(const FProject_JUIContext &Context) const
{
	return IsUsable() && (ClassId.IsNone() || ClassId == Context.ClassId) &&
		   (AdvancementId.IsNone() || AdvancementId == Context.AdvancementId) &&
		   Context.OwnedTags.HasAll(RequiredTags) && !Context.OwnedTags.HasAny(BlockedTags);
}
int32 UProject_JCharacterUIProfile::Specificity() const
{
	return (!ClassId.IsNone() ? 1 : 0) + (!AdvancementId.IsNone() ? 1 : 0) + RequiredTags.Num() + BlockedTags.Num();
}
UProject_JCharacterUIProfile *UProject_JUIProfileCatalog::Resolve(const FProject_JUIContext &Context) const
{
	TMap<FName, int32> Counts;
	for (UProject_JCharacterUIProfile *Profile : Profiles)
		if (Profile)
			++Counts.FindOrAdd(Profile->ProfileId);
	UProject_JCharacterUIProfile *Best = nullptr;
	for (UProject_JCharacterUIProfile *Profile : Profiles)
	{
		// Duplicate IDs are ambiguous, including their saved layout key. Fail closed for those entries.
		if (!Profile || Counts[Profile->ProfileId] != 1 || !Profile->Matches(Context))
			continue;
		if (!Best || Profile->Priority > Best->Priority ||
			(Profile->Priority == Best->Priority &&
			 (Profile->Specificity() > Best->Specificity() ||
			  (Profile->Specificity() == Best->Specificity() && Profile->ProfileId.LexicalLess(Best->ProfileId)))))
			Best = Profile;
	}
	return Best;
}
TArray<FProject_JUIResourceState> ProjectJUI::ReadResources(const UAbilitySystemComponent *ASC,
															const TArray<FProject_JUIResourceDefinition> &Definitions)
{
	TArray<FProject_JUIResourceState> Result;
	if (!ASC)
		return Result;
	for (const auto &Definition : Definitions)
	{
		if (!Definition.Current.IsValid() || !Definition.Maximum.IsValid() ||
			!ASC->HasAttributeSetForAttribute(Definition.Current) ||
			!ASC->HasAttributeSetForAttribute(Definition.Maximum))
			continue;
		const float Current = ASC->GetNumericAttribute(Definition.Current),
					Maximum = ASC->GetNumericAttribute(Definition.Maximum);
		if (!FMath::IsFinite(Current) || !FMath::IsFinite(Maximum) || Maximum <= 0)
			continue;
		FProject_JUIResourceState &State = Result.AddDefaulted_GetRef();
		State.ResourceId = Definition.ResourceId;
		State.Label = Definition.Label;
		State.Current = Current;
		State.Maximum = Maximum;
		State.Fraction = FMath::Clamp(Current / Maximum, 0.f, 1.f);
		State.Color = Definition.Color;
	}
	return Result;
}
