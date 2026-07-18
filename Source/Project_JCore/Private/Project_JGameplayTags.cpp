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

	GameplayTags.State_Movement_Sprinting = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Movement.Sprinting"),
		FString("Character is currently sprinting")
	);

	GameplayTags.State_Mounted = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Mounted"),
		FString("Character is currently riding a mount")
	);

	GameplayTags.State_Mount_TakingOff = GameplayTagsManager.AddNativeGameplayTag(FName("State.Mount.TakingOff"), FString("Mount is playing its protected takeoff phase"));
	GameplayTags.State_Mount_AutoAscending = GameplayTagsManager.AddNativeGameplayTag(FName("State.Mount.AutoAscending"), FString("Mount is automatically climbing after takeoff"));
	GameplayTags.State_Mount_Flying = GameplayTagsManager.AddNativeGameplayTag(FName("State.Mount.Flying"), FString("Mount is under normal aerial control"));
	GameplayTags.State_Mount_Landing = GameplayTagsManager.AddNativeGameplayTag(FName("State.Mount.Landing"), FString("Mount is playing its protected landing phase"));

	// Combat Tags
	GameplayTags.State_CombatMode = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.CombatMode"),
		FString("Character is in combat mode")
	);

	GameplayTags.State_Attacking = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Attacking"),
		FString("Character is attacking")
	);

	GameplayTags.State_Dodging = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Dodging"),
		FString("Character is dodging")
	);

	GameplayTags.State_HitReacting = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.HitReacting"),
		FString("Character is reacting to a hit")
	);

	GameplayTags.State_Dead = GameplayTagsManager.AddNativeGameplayTag(
		FName("State.Dead"),
		FString("Character is dead")
	);

	// Input Tags
	GameplayTags.InputTag_Weapon_LightAttack = GameplayTagsManager.AddNativeGameplayTag(
		FName("InputTag.Weapon.LightAttack"),
		FString("Input for Light Attack")
	);

	GameplayTags.InputTag_Weapon_HeavyAttack = GameplayTagsManager.AddNativeGameplayTag(
		FName("InputTag.Weapon.HeavyAttack"),
		FString("Input for Heavy Attack")
	);

	GameplayTags.InputTag_Skill_Dash = GameplayTagsManager.AddNativeGameplayTag(
		FName("InputTag.Skill.Dash"),
		FString("Input for Dash Skill")
	);

	// Event Tags
	GameplayTags.Event_Combat_ComboWindow = GameplayTagsManager.AddNativeGameplayTag(
		FName("Event.Combat.ComboWindow"),
		FString("Event fired when combo window is open in an animation")
	);

	GameplayTags.Event_Mount_TakeOffImpulse = GameplayTagsManager.AddNativeGameplayTag(FName("Event.Mount.TakeOffImpulse"), FString("Animation timing cue for a mount takeoff impulse"));
	GameplayTags.Event_Mount_LandingTouchdown = GameplayTagsManager.AddNativeGameplayTag(FName("Event.Mount.LandingTouchdown"), FString("Animation timing cue for a mount landing touchdown"));
}
