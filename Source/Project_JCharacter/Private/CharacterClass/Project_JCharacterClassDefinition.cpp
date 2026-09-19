#include "CharacterClass/Project_JCharacterClassDefinition.h"

bool ProjectJ::ValidateAdvancementGraph(const TArray<UProject_JCharacterClassDefinition*>& Classes,
	const TArray<UProject_JCharacterAdvancementDefinition*>& Advancements, TArray<FText>& Errors)
{
	const int32 InitialErrors = Errors.Num();
	TMap<FName, const UProject_JCharacterClassDefinition*> ByClass;
	TMap<FName, const UProject_JCharacterAdvancementDefinition*> ById;
	auto Error = [&](const FString& Message) { Errors.Add(FText::FromString(Message)); };
	for (const auto* Class : Classes)
	{
		if (!Class || Class->ClassId.IsNone() || ByClass.Contains(Class->ClassId))
		{ Error(TEXT("Classes contain a null definition, empty ID, or duplicate ID.")); continue; }
		ByClass.Add(Class->ClassId, Class);
		if (Class->SchemaVersion < 1 || Class->StartingLevel < 1) Error(TEXT("Invalid class schema version or starting level: ") + Class->ClassId.ToString());
	}
	for (const auto* Advancement : Advancements)
	{
		if (!Advancement || Advancement->AdvancementId.IsNone() || ById.Contains(Advancement->AdvancementId))
		{ Error(TEXT("Advancements contain a null definition, empty ID, or duplicate ID.")); continue; }
		ById.Add(Advancement->AdvancementId, Advancement);
		if (Advancement->SchemaVersion < 1 || Advancement->RequiredLevel < 1) Error(TEXT("Invalid advancement schema version or required level: ") + Advancement->AdvancementId.ToString());
		if (Advancement->BaseClass && ByClass.FindRef(Advancement->BaseClass->ClassId) != Advancement->BaseClass)
			Error(TEXT("Advancement must reference the exact registered base class: ") + Advancement->AdvancementId.ToString());
		if (Advancement->bOverrideEquippedGameplay && !Advancement->CombatStyleOverride)
			Error(TEXT("Equipped gameplay override requires a combat style: ") + Advancement->AdvancementId.ToString());
		if (Advancement->RequiredTags.HasAny(Advancement->BlockedTags))
			Error(TEXT("Advancement requires a blocked tag: ") + Advancement->AdvancementId.ToString());
	}
	TMap<FName, int32> InDegree;
	TMap<FName, TArray<FName>> Dependants;
	for (const auto& Pair : ById)
	{
		InDegree.Add(Pair.Key, Pair.Value->RequiredAdvancementIds.Num());
		TSet<FName> Seen;
		for (FName Prerequisite : Pair.Value->RequiredAdvancementIds)
		{
			const auto* const* Parent = ById.Find(Prerequisite);
			if (!Parent || Prerequisite == Pair.Key || Seen.Contains(Prerequisite))
				Error(TEXT("Missing, self, or duplicate prerequisite on: ") + Pair.Key.ToString());
			else if ((*Parent)->BaseClass && Pair.Value->BaseClass && (*Parent)->BaseClass != Pair.Value->BaseClass)
				Error(TEXT("Prerequisite belongs to another base class: ") + Pair.Key.ToString());
			Seen.Add(Prerequisite);
			Dependants.FindOrAdd(Prerequisite).Add(Pair.Key);
		}
	}
	// Iterative topological traversal avoids recursion on large authoring graphs.
	TArray<FName> Ready;
	for (const auto& Pair : InDegree) if (Pair.Value == 0) Ready.Add(Pair.Key);
	for (int32 Index = 0; Index < Ready.Num(); ++Index)
	{
		if (const auto* Children = Dependants.Find(Ready[Index]))
			for (FName Child : *Children) if (--InDegree.FindChecked(Child) == 0) Ready.Add(Child);
	}
	if (Ready.Num() != ById.Num()) Error(TEXT("Advancement prerequisites contain a cycle or an unresolved dependency."));
	return Errors.Num() == InitialErrors;
}
