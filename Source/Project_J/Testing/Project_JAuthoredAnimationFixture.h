#pragma once

// Read-only authored fixture shared by CPU and real-socket validation. It spawns
// the Blueprint itself, including its construction script and visual followers.
#include "Tests/Project_JMoverFrameFixture.h"
#include "AIController.h"
#include "Engine/World.h"
#include "Project_JPlayerCharacter.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/AnimInstance.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Retargeter/IKRetargeter.h"

namespace ProjectJAuthoredAnimationFixture
{
inline AProject_JPlayerCharacter* Spawn(UWorld* World, AController* Controller, AProject_JPlayerState* State, const FVector& Location, bool bSimulate = false)
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
		// must remain observable during those camera swaps. 1000m covers the fixture
		// while staying within Iris's default supported 1500m distance bound.
		Player->SetNetCullDistanceSquared(FMath::Square(100000.0f));
	}
	Player->SetPlayerState(State);
	Player->SetActorEnableCollision(bSimulate || World->GetNetMode() != NM_Standalone);
	Player->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
	Controller->Possess(Player);
	State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	State->GetEquipmentManagerComponent()->EquipItem(Item);
	// Static standalone diagnostics prescribe a frame. Real socket fixtures use Mover simulation.
	if (!bSimulate && World->GetNetMode()==NM_Standalone && !ProjectJ::Tests::PublishMoverFrame(Player,MOVE_None)) { Player->Destroy(); return nullptr; }
	return Player;
}

#if WITH_EDITOR
// Editor-only preview meshes are not a cooked runtime mesh source.
// Exercise the explicitly authored incompatible-body bridge independently of
// which compatible avatar is currently selected by BP_GreatSword. All changes
// belong to this transient test actor; no Blueprint or mesh asset is modified.
inline AProject_JPlayerCharacter* SpawnRetargetFallback(UWorld* World, AController* Controller,
	AProject_JPlayerState* State, const FVector& Location)
{
	auto* Family=LoadObject<UProject_JRetargetFamilyProfile>(nullptr,TEXT("/Game/DataAssetSets/Presentation/Retarget/DA_RetargetFamily_Mannequin_Bip01.DA_RetargetFamily_Mannequin_Bip01"));
	UClass* BodyClass=LoadClass<UAnimInstance>(nullptr,TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.ABP_Greatsword_Woman_RunTIme_C"));
	if (!Family || !Family->Retargeter || !BodyClass) return nullptr;
	auto* SourceAsset=const_cast<USkeletalMesh*>(Family->Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source));
	auto* TargetAsset=const_cast<USkeletalMesh*>(Family->Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target));
	if (!SourceAsset || !TargetAsset || !Family->Matches(SourceAsset->GetSkeleton(),TargetAsset->GetSkeleton())) return nullptr;
	auto* Player=Spawn(World,Controller,State,Location);if (!Player) return nullptr;
	TInlineComponentArray<UProject_JModularMeshComponent*> Parts(Player);
	for (auto* Part:Parts) Part->DestroyComponent();
	auto* Source=Player->GetMesh();Source->SetSkeletalMeshAsset(SourceAsset);
	Source->SetHiddenInGame(true);Source->SetVisibility(false);
	auto* Visual=NewObject<USkeletalMeshComponent>(Player,NAME_None,RF_Transient);
	Visual->SetupAttachment(Source);Visual->ComponentTags.Add(TEXT("VisualFollower"));
	Visual->SetSkeletalMeshAsset(TargetAsset);
	// This follower graph owns contact and body physics. A mesh-level direct
	// post process must never create a second writer on the same component.
	Visual->SetDisablePostProcessBlueprint(true);
	Visual->SetAnimInstanceClass(BodyClass);Player->AddInstanceComponent(Visual);
	Visual->RegisterComponent();Visual->AddTickPrerequisiteComponent(Source);
	if (auto* Presentation=Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()) Presentation->RefreshAttachmentCalibration();
	return Player;
}
#endif
}
