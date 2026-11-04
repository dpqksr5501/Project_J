#pragma once

// Read-only authored fixture shared by CPU and real-socket validation. It spawns
// the Blueprint itself, including its construction script and visual followers.
#include "Tests/Project_JMoverFrameFixture.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameFramework/MovementInterface.h"
#include "GameFramework/MovementComponent.h"
#include "Engine/OverlapResult.h"
#include "Movement/Project_JMovementQueries.h"
#include "UObject/StrongObjectPtr.h"
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
inline AProject_JPlayerCharacter* Spawn(UWorld* World, AController* Controller, AProject_JPlayerState* State, const FVector& Location, bool bSimulate = false, bool bAdmitNativeSpawn = false, FString* OutSpawnFailure = nullptr)
{
	UClass* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
	auto* Item = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!Class || !Item || !World || !Controller || !State)
    { if(bAdmitNativeSpawn && OutSpawnFailure) *OutSpawnFailure=TEXT("spawn.required_input_missing");return nullptr; }
    const TStrongObjectPtr<UWorld> KeepWorld(bAdmitNativeSpawn?World:nullptr);
    const TStrongObjectPtr<AController> KeepController(bAdmitNativeSpawn?Controller:nullptr);
    const TStrongObjectPtr<AProject_JPlayerState> KeepState(bAdmitNativeSpawn?State:nullptr);
    const TStrongObjectPtr<APawn> PreviousPawn(bAdmitNativeSpawn?Controller->GetPawn():nullptr);
    FVector SpawnLocation=Location;
    if(bAdmitNativeSpawn)
    {
        // Native CDO placement happens before Mover component initialization. Finish may reject, never relocate it.
        const auto* Template=Class->GetDefaultObject<AProject_JPlayerCharacter>();
        const auto* TemplateCapsule=Template?Cast<UCapsuleComponent>(Template->GetRootComponent()):nullptr;
        const bool OwnerCurrent=IsValid(World) && !World->bIsTearingDown && IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->GetWorld()==World &&
            IsValid(State) && !State->IsActorBeingDestroyed() && State->GetWorld()==World && Controller->GetPlayerState<AProject_JPlayerState>()==State;
        const bool TemplateQueryable=TemplateCapsule && TemplateCapsule->IsQueryCollisionEnabled();
        if(!OwnerCurrent || !TemplateQueryable || Location.ContainsNaN())
        { if(OutSpawnFailure) *OutSpawnFailure=FString::Printf(TEXT("spawn.native_initial_guard_rejected owner=%d template_query=%d finite=%d requested=%s"),OwnerCurrent,TemplateQueryable,!Location.ContainsNaN(),*Location.ToString());return nullptr; }
        if(!World->FindTeleportSpot(Template,SpawnLocation,FRotator::ZeroRotator))
        {
            if(OutSpawnFailure)
            {
                // One failed native request only. Raw capsule overlaps do not impersonate the engine's all-component decision.
                float Radius=0,HalfHeight=0;TemplateCapsule->GetScaledCapsuleSize(Radius,HalfHeight);
                TArray<FOverlapResult> Overlaps;
                World->OverlapMultiByChannel(Overlaps,Location,FQuat::Identity,TemplateCapsule->GetCollisionObjectType(),TemplateCapsule->GetCollisionShape(),
                    FCollisionQueryParams(SCENE_QUERY_STAT(ProjectJNativeSpawnFailure),false,Template),FCollisionResponseParams(TemplateCapsule->GetCollisionResponseToChannels()));
                FString Blocking,Children;int32 BlockingCount=0,Shown=0,QueryChildren=0;
                for(const auto& Overlap:Overlaps) if(Overlap.bBlockingHit)
                { ++BlockingCount;if(Shown++<8) Blocking+=FString::Printf(TEXT("[%s/%s]"),*GetNameSafe(Overlap.GetActor()),*GetNameSafe(Overlap.GetComponent())); }
                TInlineComponentArray<UPrimitiveComponent*> Primitives;Template->GetComponents(Primitives);
                for(int32 I=0;I<FMath::Min(Primitives.Num(),32);++I)
                    if(const auto* Primitive=Primitives[I];Primitive && Primitive!=TemplateCapsule && Primitive->IsQueryCollisionEnabled())
                    { if(QueryChildren++<8) Children+=FString::Printf(TEXT("[%s]"),*GetNameSafe(Primitive)); }
                const auto* Mover=Project_J::Movement::FindMover(Template);
                const auto* Movement=Template->FindComponentByInterface<IMovementInterface>();
                const auto* Legacy=Template->FindComponentByClass<UMovementComponent>();
                *OutSpawnFailure=FString::Printf(TEXT("spawn.native_initial_placement_rejected requested=%s radius=%.9g halfheight=%.9g channel=%d profile=%s root=%s actor_collision=%d mover_updated=%s interface_bound=%d interface_updated=%s legacy_move=%s legacy_updated=%s capsule_blocking=%d shown_max8=%s template_components=%d scanned_max32 query_children=%d shown_max8=%s"),
                    *Location.ToString(),Radius,HalfHeight,int32(TemplateCapsule->GetCollisionObjectType()),*TemplateCapsule->GetCollisionProfileName().ToString(),*GetNameSafe(TemplateCapsule),Template->GetActorEnableCollision(),
                    *GetNameSafe(Mover?Mover->GetUpdatedComponent():nullptr),Movement!=nullptr,*GetNameSafe(Movement?Movement->GetUpdatedObject():nullptr),
                    *GetNameSafe(Legacy),*GetNameSafe(Legacy?Legacy->UpdatedPrimitive.Get():nullptr),BlockingCount,*Blocking,Primitives.Num(),QueryChildren,*Children);
            }
            return nullptr;
        }
        if(SpawnLocation.ContainsNaN())
        { if(OutSpawnFailure) *OutSpawnFailure=TEXT("spawn.native_initial_result_nonfinite");return nullptr; }
    }
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = bAdmitNativeSpawn?ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding:ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	auto* Player = World->SpawnActor<AProject_JPlayerCharacter>(Class, SpawnLocation, FRotator::ZeroRotator, Params);
	if (!Player)
    { if(bAdmitNativeSpawn && OutSpawnFailure) *OutSpawnFailure=FString::Printf(TEXT("spawn.native_actor_creation_rejected requested=%s admitted=%s"),*Location.ToString(),*SpawnLocation.ToString());return nullptr; }
    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(bAdmitNativeSpawn?Player:nullptr);
	if (World->GetNetMode() != NM_Standalone)
	{
		// The AOI NPC workload uses clusters 200m apart; both owned player actors
		// must remain observable during those camera swaps. 1000m covers the fixture
		// while staying within Iris's default supported 1500m distance bound.
		Player->SetNetCullDistanceSquared(FMath::Square(100000.0f));
	}
	if(!bAdmitNativeSpawn) Player->SetPlayerState(State); // Default helper lifecycle is unchanged.
	Player->SetActorEnableCollision(bSimulate || World->GetNetMode() != NM_Standalone);
	Player->FinishSpawning(FTransform(FRotator::ZeroRotator, SpawnLocation));
    if(bAdmitNativeSpawn)
    {
        const auto Reject=[&](const TCHAR* Reason)
        {
            if(OutSpawnFailure) *OutSpawnFailure=FString::Printf(TEXT("spawn.%s candidate=%s requested=%s admitted=%s actual=%s"),Reason,*GetNameSafe(Player),
                *Location.ToString(),*SpawnLocation.ToString(),*Player->GetActorLocation().ToString());
            if(IsValid(Player) && !Player->IsActorBeingDestroyed()) Player->Destroy();
            return static_cast<AProject_JPlayerCharacter*>(nullptr);
        };
        if(!IsValid(Player) || Player->IsActorBeingDestroyed()) return Reject(TEXT("native_finish_rejected"));
        if(World->bIsTearingDown || !IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld()!=World ||
           !IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld()!=World || Controller->GetPlayerState<AProject_JPlayerState>()!=State ||
           Controller->GetPawn()!=PreviousPawn.Get() || Player->GetWorld()!=World || Player->GetPlayerState()!=nullptr || Player->GetController() ||
           !Player->GetActorLocation().Equals(SpawnLocation,UE_KINDA_SMALL_NUMBER)) return Reject(TEXT("owner_or_initial_transform_changed"));
        const auto* Capsule=Project_J::Movement::FindCapsule(Player);
        float Radius=0,HalfHeight=0;if(Capsule) Capsule->GetScaledCapsuleSize(Radius,HalfHeight);
        if(!IsValid(Capsule) || Capsule->IsBeingDestroyed() || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled() ||
           !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius<=0 || HalfHeight<Radius ||
           World->EncroachingBlockingGeometry(Player,Player->GetActorLocation(),Player->GetActorRotation())) return Reject(TEXT("actual_capsule_not_clear"));
        const FVector Origin=Capsule->GetComponentLocation(),Up=Capsule->GetUpVector();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJFixtureNativeSpawn),false,Player);
        auto Responses=FCollisionResponseParams(Capsule->GetCollisionResponseToChannels());Responses.CollisionResponse.SetResponse(ECC_Pawn,ECR_Ignore);
        FHitResult Ground;
        if(!World->LineTraceSingleByChannel(Ground,Origin,Origin-Up*(HalfHeight+Radius),Capsule->GetCollisionObjectType(),Query,Responses) ||
           !Ground.bBlockingHit || Ground.bStartPenetrating || Ground.ImpactPoint.ContainsNaN() || Ground.ImpactNormal.ContainsNaN() ||
           Cast<APawn>(Ground.GetActor()) || !Project_J::Movement::IsWalkable(Player,Ground)) return Reject(TEXT("actual_capsule_has_no_walkable_nonpawn_support"));
        // Publish the PlayerState only after the physical candidate is admitted; never restore/reset a rejected owner.
        Player->SetPlayerState(State);
        if(!IsValid(Player) || Player->IsActorBeingDestroyed() || World->bIsTearingDown || !IsValid(Controller) || Controller->IsActorBeingDestroyed() ||
           !IsValid(State) || State->IsActorBeingDestroyed() || Controller->GetPawn()!=PreviousPawn.Get() ||
           Controller->GetPlayerState<AProject_JPlayerState>()!=State || Player->GetPlayerState()!=State || State->GetPawn()!=Player)
            return Reject(TEXT("owner_changed_during_state_publication"));
    }
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
	UClass* BodyClass=LoadClass<UAnimInstance>(nullptr,TEXT("/Game/Animation_Logic/ABPs/Spellblade/ABP_Spellblade_Woman_RunTIme.ABP_Spellblade_Woman_RunTIme_C"));
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
