#include "Equipment/Project_JWeaponPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AProject_JWeaponPresentationActor::AProject_JWeaponPresentationActor()
{
	// Presentation is reconstructed by the equipped character on each client.
	// Do not replicate a second actor or a per-frame transform for cosmetic motion.
	bReplicates = false;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	WeaponRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponRoot"));
	SetRootComponent(WeaponRoot);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(WeaponRoot);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetCanEverAffectNavigation(false);
	WeaponMesh->SetIsReplicated(false);
}

void AProject_JWeaponPresentationActor::ReleaseLocalLOD()
{
	if (bOwnsLocalLOD0 && WeaponMesh && WeaponMesh->GetForcedLodModel()==1) WeaponMesh->SetForcedLodModel(0);
	bOwnsLocalLOD0=false;
}
void AProject_JWeaponPresentationActor::RefreshLocalLOD()
{
	const auto* Pawn=LODOwner.Get();
	// PlayerState can arrive after Controller on a client. Do not make this
	// event-driven lease depend on PlayerState-backed IsPlayerControlled().
	const bool bLocalPlayer=Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	if (!bLocalPlayer) { ReleaseLocalLOD(); return; }
	// Existing authored/other-system forced LODs win. This lease owns only auto->LOD0.
	if (WeaponMesh && WeaponMesh->GetForcedLodModel()==0)
	{ WeaponMesh->SetForcedLodModel(1); bOwnsLocalLOD0=true; }
}
void AProject_JWeaponPresentationActor::BindLODOwner()
{
	if (auto* Previous=LODOwner.Get()) Previous->ReceiveControllerChangedDelegate.RemoveDynamic(this,&ThisClass::HandleLODControllerChanged);
	LODOwner=Cast<APawn>(GetOwner());
	if (auto* Pawn=LODOwner.Get()) Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this,&ThisClass::HandleLODControllerChanged);
	RefreshLocalLOD();
}
void AProject_JWeaponPresentationActor::HandleLODControllerChanged(APawn*,AController*,AController*) { RefreshLocalLOD(); }
void AProject_JWeaponPresentationActor::SetOwner(AActor* NewOwner)
{
	ReleaseLocalLOD();
	if (auto* Previous=LODOwner.Get()) Previous->ReceiveControllerChangedDelegate.RemoveDynamic(this,&ThisClass::HandleLODControllerChanged);
	LODOwner.Reset(); Super::SetOwner(NewOwner);
	if (HasActorBegunPlay()) BindLODOwner();
}
void AProject_JWeaponPresentationActor::BeginPlay() { Super::BeginPlay(); BindLODOwner(); }
void AProject_JWeaponPresentationActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Pawn=LODOwner.Get()) Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this,&ThisClass::HandleLODControllerChanged);
	LODOwner.Reset(); ReleaseLocalLOD(); Super::EndPlay(Reason);
}
