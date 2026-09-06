#include "Equipment/Project_JWeaponPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

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
