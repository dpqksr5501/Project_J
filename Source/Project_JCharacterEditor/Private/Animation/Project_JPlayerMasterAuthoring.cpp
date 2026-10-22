#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Project_JPlayerCharacter.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

bool UProject_JPlayerLocomotionLibrary::PreparePlayerMaster(UBlueprint* Target)
{
	if ((GEditor && GEditor->PlayWorld) || !Target || Target->ParentClass != AProject_JPlayerCharacter::StaticClass() ||
		!Target->GeneratedClass || !Target->NewVariables.IsEmpty() ||
		(Target->GetPathName() != TEXT("/Game/Character_BPs/BP_Player.BP_Player") &&
		 Target->GetPathName() != TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master"))) return false;
	auto* Defaults = Target->GeneratedClass->GetDefaultObject();
	const auto* Native = AProject_JPlayerCharacter::StaticClass()->GetDefaultObject();
	// This only prepares the shared base. Job data and profile assets are never changed.
	const auto* Job = FindFProperty<FObjectPropertyBase>(Target->GeneratedClass, TEXT("CharacterClassDefinition"));
	if (!Job || Job->GetObjectPropertyValue_InContainer(Defaults)) return false;
	TArray<FProperty*> Properties;
	for (const FName Name : {FName(TEXT("DefaultAbilities")), FName(TEXT("DefaultAttributeData")),
		FName(TEXT("AttributeSet")), FName(TEXT("AbilitySystemComponent")),
		FName(TEXT("EquipmentManager")), FName(TEXT("LocomotionProfile"))})
	{
		auto* Property = FindFProperty<FProperty>(AProject_JPlayerCharacter::StaticClass(), Name);
		if (!Property) return false;
		Properties.Add(Property);
	}
	Target->Modify(); Defaults->Modify();
	for (auto* Property : Properties) Property->CopyCompleteValue_InContainer(Defaults, Native);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Target);
	FKismetEditorUtilities::CompileBlueprint(Target);
	// Class-owned inline templates become archetypes of child BP defaults. UE's
	// package contract requires archetypes to be public when another package inherits them.
	FinalizePlayerAvatarTemplates(Target);
	return Target->Status == BS_UpToDate;
}

int32 UProject_JPlayerLocomotionLibrary::FinalizePlayerAvatarTemplates(UBlueprint* Target)
{
	if ((GEditor && GEditor->PlayWorld) || !Target || !Target->GeneratedClass ||
		!Target->GeneratedClass->IsChildOf(AProject_JPlayerCharacter::StaticClass())) return INDEX_NONE;
	int32 Changed = 0;
	ForEachObjectWithOuter(Target->GeneratedClass->GetDefaultObject(), [&Changed](UObject* Object)
	{
		if (Object->HasAnyFlags(RF_ArchetypeObject) && !Object->HasAnyFlags(RF_Public))
		{
			Object->Modify(); Object->SetFlags(RF_Public); ++Changed;
		}
	}, EGetObjectsFlags::IncludeNestedObjects);
	if (Changed) Target->MarkPackageDirty();
	return Changed;
}
