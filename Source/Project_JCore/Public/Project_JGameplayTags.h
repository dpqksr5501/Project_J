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
	FGameplayTag State_Movement_Sprinting;

	// Combat States
	FGameplayTag State_CombatMode;
	FGameplayTag State_Attacking;
	FGameplayTag State_Dodging;
	FGameplayTag State_HitReacting;
	FGameplayTag State_Dead;

	// Input Tags
	FGameplayTag InputTag_Weapon_LightAttack;
	FGameplayTag InputTag_Weapon_HeavyAttack;
	FGameplayTag InputTag_Skill_Dash;

	// Event Tags
	FGameplayTag Event_Combat_ComboWindow;

private:
	static FProject_JGameplayTags GameplayTags;
};
