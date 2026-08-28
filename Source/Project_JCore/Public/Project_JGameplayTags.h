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
	FGameplayTag State_Mounted;
	FGameplayTag State_Mount_TakingOff;
	FGameplayTag State_Mount_AutoAscending;
	FGameplayTag State_Mount_Flying;
	FGameplayTag State_Mount_Landing;

	// Combat States
	FGameplayTag State_CombatMode;
	FGameplayTag State_CombatTransition;
	FGameplayTag State_Attacking;
	FGameplayTag State_Dodging;
	FGameplayTag State_HitReacting;
	FGameplayTag State_Dead;

	// Input Tags
	FGameplayTag InputTag_Weapon_LMB;
	FGameplayTag InputTag_Weapon_RMB;
	FGameplayTag InputTag_Skill_Q;
	FGameplayTag InputTag_Skill_R;
	FGameplayTag InputTag_Skill_T;
	FGameplayTag InputTag_Skill_Dash;

	// Event Tags
	FGameplayTag Event_Combat_ComboWindow;
	FGameplayTag Event_Mount_TakeOffImpulse;
	FGameplayTag Event_Mount_LandingTouchdown;

private:
	static FProject_JGameplayTags GameplayTags;
};
