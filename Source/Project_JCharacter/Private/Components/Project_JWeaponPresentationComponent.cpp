#include "Components/Project_JWeaponPresentationComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"

UProject_JWeaponPresentationComponent::UProject_JWeaponPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JWeaponPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitCombatPresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JWeaponPresentationComponent::EnterCombatPresentation()
{
	bCombatPresentationActive = true;
	UE_LOG(LogTemp, Log, TEXT("[ProjectJ][WeaponPresentation] EnterCombatPresentation: Owner=%s"), *GetNameSafe(GetOwner()));
	RefreshPresentation();
}

void UProject_JWeaponPresentationComponent::ExitCombatPresentation()
{
	bCombatPresentationActive = false;
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}
}

void UProject_JWeaponPresentationComponent::BeginSheathePresentation()
{
	// Do not destroy yet: an authored montage notify will move this actor from
	// the hand to the back socket at the intended animation frame.
	bCombatPresentationActive = false;
}

void UProject_JWeaponPresentationComponent::AttachWeaponToSheathedSocket()
{
	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!SpawnedWeapon || !PresentationProfile || PresentationProfile->SheathedSocketName.IsNone() || !Mesh)
	{
		return;
	}

	if (!Mesh->DoesSocketExist(PresentationProfile->SheathedSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Sheathe failed: Socket '%s' does not exist on Mesh=%s."),
			*PresentationProfile->SheathedSocketName.ToString(), *GetNameSafe(Mesh));
		return;
	}

	SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->SheathedSocketName);
}

void UProject_JWeaponPresentationComponent::RefreshPresentation()
{
	if (!ShouldShowWeapon())
	{
		return;
	}
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}

	const UProject_JWeaponPresentationProfile* PresentationProfile = GetCurrentPresentationProfile();
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	if (!PresentationProfile || !PresentationProfile->WeaponActorClass || PresentationProfile->DrawnSocketName.IsNone() || !Mesh || !GetWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: Owner=%s Profile=%s ActorClass=%s Socket=%s Mesh=%s World=%s"),
			*GetNameSafe(OwnerCharacter),
			*GetNameSafe(PresentationProfile),
			PresentationProfile ? *GetNameSafe(PresentationProfile->WeaponActorClass) : TEXT("None"),
			PresentationProfile ? *PresentationProfile->DrawnSocketName.ToString() : TEXT("None"),
			*GetNameSafe(Mesh),
			*GetNameSafe(GetWorld()));
		return;
	}

	if (!Mesh->DoesSocketExist(PresentationProfile->DrawnSocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: Socket '%s' does not exist on Mesh=%s (SkeletalMesh=%s)."),
			*PresentationProfile->DrawnSocketName.ToString(),
			*GetNameSafe(Mesh),
			*GetNameSafe(Mesh->GetSkeletalMeshAsset()));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = OwnerCharacter;
	SpawnedWeapon = GetWorld()->SpawnActor<AActor>(PresentationProfile->WeaponActorClass, OwnerCharacter->GetActorLocation(), OwnerCharacter->GetActorRotation(), SpawnParams);
	if (SpawnedWeapon)
	{
		SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, PresentationProfile->DrawnSocketName);
		UE_LOG(LogTemp, Log, TEXT("[ProjectJ][WeaponPresentation] Draw success: Weapon=%s Socket=%s"),
			*GetNameSafe(SpawnedWeapon), *PresentationProfile->DrawnSocketName.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectJ][WeaponPresentation] Draw failed: SpawnActor returned null for class %s."),
			*GetNameSafe(PresentationProfile->WeaponActorClass));
	}
}

const UProject_JWeaponPresentationProfile* UProject_JWeaponPresentationComponent::GetCurrentPresentationProfile() const
{
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetOwner());
	return PlayerCharacter ? PlayerCharacter->GetCurrentWeaponPresentationProfile() : nullptr;
}

bool UProject_JWeaponPresentationComponent::ShouldShowWeapon() const
{
	return bCombatPresentationActive;
}
