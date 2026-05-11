#include "Project_JGameplayTags.h"
#include "GameplayTagsManager.h"

FProject_JGameplayTags FProject_JGameplayTags::GameplayTags;

void FProject_JGameplayTags::InitializeNativeGameplayTags()
{
	UGameplayTagsManager& GameplayTagsManager = UGameplayTagsManager::Get();

	GameplayTags.State_Movement_Landing = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Movement.Landing"),
		FString("Character is currently playing a landing animation")
	);

	GameplayTags.State_Movement_InAir = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Movement.InAir"),
		FString("Character is currently in the air")
	);
}
