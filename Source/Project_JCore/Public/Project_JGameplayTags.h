#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UGameplayTagsManager;

/**
 * Project_JGameplayTags
 *
 * Singleton containing native Gameplay Tags
 */
struct PROJECT_JCORE_API FProject_JGameplayTags
{
public:
	static const FProject_JGameplayTags& Get() { return GameplayTags; }
	static void InitializeNativeGameplayTags();

	FGameplayTag State_Movement_Landing;
	FGameplayTag State_Movement_InAir;

private:
	static FProject_JGameplayTags GameplayTags;
};
