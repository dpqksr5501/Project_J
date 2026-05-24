#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

UProject_JEquipmentManagerComponent::UProject_JEquipmentManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JEquipmentManagerComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UProject_JEquipmentManagerComponent::EquipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || EquippedMeshes.Contains(ItemDef))
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	USkeletalMeshComponent* MainMesh = OwnerCharacter->GetMesh();
	if (!MainMesh) return;

	// Spawn the modular mesh component dynamically
	UProject_JModularMeshComponent* NewMeshComp = NewObject<UProject_JModularMeshComponent>(OwnerCharacter);
	NewMeshComp->RegisterComponent();

	// Load and set the skeletal mesh asset
	if (!ItemDef->EquipmentMesh.IsNull())
	{
		// Note: In production, load asynchronously instead of LoadSynchronous
		NewMeshComp->SetSkeletalMesh(ItemDef->EquipmentMesh.LoadSynchronous());
	}

	// Attach to specific socket or standard Leader Pose
	if (ItemDef->AttachSocketName.IsNone())
	{
		// Standard armor/body part: Follow leader pose
		NewMeshComp->AttachAndSetLeader(MainMesh);
	}
	else
	{
		// Weapon or accessory: Attach to socket and disable leader pose (usually rigid)
		NewMeshComp->AttachToComponent(MainMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ItemDef->AttachSocketName);
		NewMeshComp->SetLeaderPoseComponent(nullptr); 
	}

	EquippedMeshes.Add(ItemDef, NewMeshComp);

	// Inject Animation Profile to the Character's Locomotion/Anim State Component
	if (ItemDef->WeaponAnimProfile)
	{
		// Since Project_JPlayerCharacter already has extensive logic to handle WeaponAnimProfiles,
		// you would cast OwnerCharacter to AProject_JPlayerCharacter and pass the new WeaponAnimProfile
		// so it updates its Motion Matching Chooser Table overrides automatically.
		
		// Example:
		// AProject_JPlayerCharacter* PlayerChar = Cast<AProject_JPlayerCharacter>(OwnerCharacter);
		// if (PlayerChar) { PlayerChar->SetWeaponAnimProfile(ItemDef->WeaponAnimProfile); }
	}

	// Grant GAS Abilities
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
	if (ASC && OwnerCharacter->HasAuthority())
	{
		// In a real implementation, you would store the FGameplayAbilitySpecHandle to revoke later
		for (TSubclassOf<UGameplayAbility> AbilityClass : ItemDef->GrantedAbilities)
		{
			// ASC->GiveAbility(...)
		}
	}
}

void UProject_JEquipmentManagerComponent::UnequipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef) return;

	if (UProject_JModularMeshComponent** FoundMesh = EquippedMeshes.Find(ItemDef))
	{
		if (*FoundMesh)
		{
			(*FoundMesh)->DestroyComponent();
		}
		EquippedMeshes.Remove(ItemDef);

		// Also remove granted GAS abilities here.
	}
}
