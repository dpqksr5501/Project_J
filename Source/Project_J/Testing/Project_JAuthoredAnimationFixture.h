#pragma once

// Read-only authored fixture shared by CPU and real-socket validation. It spawns
// the Blueprint itself, including its construction script and visual followers.
#include "AIController.h"
#include "Project_JPlayerCharacter.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace ProjectJAuthoredAnimationFixture
{
inline AProject_JPlayerCharacter* Spawn(UWorld* World, AController* Controller, AProject_JPlayerState* State, const FVector& Location)
{
	UClass* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	auto* Item = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!Class || !Item || !World || !Controller || !State) { return nullptr; }
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	auto* Player = World->SpawnActor<AProject_JPlayerCharacter>(Class, Location, FRotator::ZeroRotator, Params);
	if (!Player) { return nullptr; }
	if (World->GetNetMode() != NM_Standalone)
	{
		// The AOI NPC workload uses clusters 200m apart; both owned player actors
		// must remain observable during those camera swaps. Set their cull distance
		// before FinishSpawning, when the initial replication filter is configured.
		Player->SetNetCullDistanceSquared(1.0e12f);
	}
	Player->SetPlayerState(State);
	Player->SetActorEnableCollision(World->GetNetMode() != NM_Standalone);
	Player->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
	Controller->Possess(Player);
	State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	State->GetEquipmentManagerComponent()->EquipItem(Item);
	// Controlled kinematics remove navigation/ground geometry from CPU comparisons.
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Player->GetCharacterMovement()->SetComponentTickEnabled(false);
	return Player;
}
}
