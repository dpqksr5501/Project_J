#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Project_JPlayerCharacter.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "UObject/UObjectHash.h"

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
