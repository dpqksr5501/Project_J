#include "UI/Project_JCharacterUIProfile.h"
#include "AbilitySystemComponent.h"
#include "Validation/Project_JDataValidation.h"

namespace
{
struct FProfileIssue
{
	const TCHAR* Reason = nullptr;
	int32 Index = INDEX_NONE;
#if WITH_EDITOR
	FString Describe() const
	{
		return Index == INDEX_NONE ? FString(Reason) : FString::Printf(TEXT("%s (entry %d)"), Reason, Index);
	}
#endif
};

FProfileIssue FindProfileIssue(const UProject_JCharacterUIProfile& Profile)
{
	if (Profile.ProfileId.IsNone()) return {TEXT("ProfileId is missing")};
	if (Profile.bOverrideSkills && Profile.bAppendSkills) return {TEXT("Choose either Override Skills or Append Skills")};
	TMap<FGameplayTag, TSet<FName>> InputClasses;
	if (Profile.bOverrideSkills || Profile.bAppendSkills)
	{
		if (Profile.Skills.Num() > 64) return {TEXT("Skills exceeds 64 entries")};
		for (int32 Index = 0; Index < Profile.Skills.Num(); ++Index)
		{
			const auto& Slot = Profile.Skills[Index];
			if (!Slot.InputTag.IsValid()) return {TEXT("Skills has an unassigned InputTag"), Index};
			auto& Classes = InputClasses.FindOrAdd(Slot.InputTag);
			if (Classes.Contains(NAME_None) || Classes.Contains(Slot.ClassId)
				|| (Slot.ClassId.IsNone() && !Classes.IsEmpty()))
				return {TEXT("Skills reuses an InputTag for overlapping classes"), Index};
			Classes.Add(Slot.ClassId);
		}
	}
	if (Profile.bOverrideResources)
	{
		if (Profile.Resources.Num() > 6) return {TEXT("Resources exceeds 6 entries")};
		TSet<FName> IDs;
		for (int32 Index = 0; Index < Profile.Resources.Num(); ++Index)
		{
			const auto& Resource = Profile.Resources[Index];
			if (Resource.ResourceId.IsNone() || IDs.Contains(Resource.ResourceId))
				return {TEXT("Resources needs a unique ResourceId"), Index};
			if (!Resource.Current.IsValid() || !Resource.Maximum.IsValid())
				return {TEXT("Resources needs both Current and Maximum attributes"), Index};
			IDs.Add(Resource.ResourceId);
		}
	}
	return {};
}
}

bool UProject_JCharacterUIProfile::IsUsable() const
{
	return FindProfileIssue(*this).Reason == nullptr;
}

#if WITH_EDITOR
EDataValidationResult UProject_JCharacterUIProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	const FProfileIssue Issue = FindProfileIssue(*this);
	if (Issue.Reason) Context.AddError(FText::FromString(Issue.Describe()));
	return Project_J::DataValidation::MakeResult(Parent, Issue.Reason != nullptr);
}

EDataValidationResult UProject_JUIProfileCatalog::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	bool bInvalid = false;
	TSet<FName> IDs;
	for (int32 Index = 0; Index < Profiles.Num(); ++Index)
	{
		const auto* Profile = Profiles[Index].Get();
		FString Problem;
		if (!Profile) Problem = TEXT("assign a profile or remove the empty entry");
		else
		{
			const FProfileIssue Issue = FindProfileIssue(*Profile);
			if (Issue.Reason) Problem = Profile->GetPathName() + TEXT(" - ") + Issue.Describe();
			else if (IDs.Contains(Profile->ProfileId))
				Problem = FString::Printf(TEXT("duplicate ProfileId '%s' on %s"), *Profile->ProfileId.ToString(), *Profile->GetPathName());
			IDs.Add(Profile->ProfileId);
		}
		if (!Problem.IsEmpty())
			Project_J::DataValidation::AddError(Context, bInvalid, FText::FromString(FString::Printf(TEXT("Profiles[%d] - %s"), Index, *Problem)));
	}
	return Project_J::DataValidation::MakeResult(Parent, bInvalid);
}
#endif
TArray<FProject_JUISkillSlot> UProject_JCharacterUIProfile::ResolveSkills(const TArray<FProject_JUISkillSlot>& Base, FName CurrentClass) const
{
	TArray<FProject_JUISkillSlot> Result = bOverrideSkills ? Skills : Base;
	Result.RemoveAll([CurrentClass](const auto& Slot) { return !Slot.ClassId.IsNone() && Slot.ClassId != CurrentClass; });
	if (bAppendSkills)
		for (const FProject_JUISkillSlot& Slot : Skills)
			if ((Slot.ClassId.IsNone() || Slot.ClassId == CurrentClass)
				&& !Result.ContainsByPredicate([&](const auto& Existing) { return Slot.InputTag == Existing.InputTag; }))
				Result.Add(Slot);
	return Result;
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
