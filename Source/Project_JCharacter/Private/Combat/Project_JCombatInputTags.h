#pragma once

#include "AbilitySystemComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

namespace Project_J::Combat
{
/** Read physical input eligibility on both the predicting and authority avatars. */
inline FGameplayTagContainer ReadInputOwnerTags(const UAbilitySystemComponent* ASC, const AProject_JPlayerCharacter* Player)
{
	FGameplayTagContainer Tags;
	if (!ASC) { return Tags; }
	ASC->GetOwnedGameplayTags(Tags);

	const auto* Mover = Player && ASC->GetAvatarActor() == Player ? Player->GetMover() : nullptr;
	if (Mover)
	{
		// The animation episode's loose tag can lag the physical transition
		// or be absent on a server that doesn't evaluate presentation.
		const FGameplayTag Air = FProject_JGameplayTags::Get().State_Movement_InAir;
		if (Mover->IsAirborne()) { Tags.AddTag(Air); }
		else if (Mover->IsOnGround()) { Tags.RemoveTag(Air); }
	}
	return Tags;
}
}
