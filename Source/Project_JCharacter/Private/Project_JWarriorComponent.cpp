// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JWarriorComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "CombatDamageable.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Combat/Project_JCombatTypes.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"

// Sets default values for this component's properties
UProject_JWarriorComponent::UProject_JWarriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void UProject_JWarriorComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called when the game ends
void UProject_JWarriorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnequipWeapon();
	Super::EndPlay(EndPlayReason);
}

void UProject_JWarriorComponent::EquipWeapon()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const TSubclassOf<AActor> EffectiveWeaponClass = GetEffectiveWeaponClass();
	if (!Owner || !EffectiveWeaponClass || !GetWorld())
	{
		return;
	}

	// Clean up existing weapon if any
	UnequipWeapon();

	// Spawn the weapon at owner's location/rotation
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.Instigator = Owner;

	SpawnedWeapon = GetWorld()->SpawnActor<AActor>(EffectiveWeaponClass, Owner->GetActorLocation(), Owner->GetActorRotation(), SpawnParams);
	if (SpawnedWeapon)
	{
		// Attach to the specified weapon socket on character mesh
		if (USkeletalMeshComponent* Mesh = Owner->GetMesh())
		{
			SpawnedWeapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetIncludingScale, GetEffectiveWeaponSocketName());
		}
	}
}

void UProject_JWarriorComponent::UnequipWeapon()
{
	if (SpawnedWeapon)
	{
		SpawnedWeapon->Destroy();
		SpawnedWeapon = nullptr;
	}
}



const UProject_JWeaponAnimProfile* UProject_JWarriorComponent::GetCurrentWeaponAnimProfile() const
{
	const AProject_JPlayerCharacter* OwnerPlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
	return OwnerPlayer ? OwnerPlayer->GetWeaponAnimProfile() : nullptr;
}

TSubclassOf<AActor> UProject_JWarriorComponent::GetEffectiveWeaponClass() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (WeaponAnimProfile->WeaponActorClass)
		{
			return WeaponAnimProfile->WeaponActorClass;
		}
	}

	return WeaponClass;
}

FName UProject_JWarriorComponent::GetEffectiveWeaponSocketName() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (!WeaponAnimProfile->WeaponSocketName.IsNone())
		{
			return WeaponAnimProfile->WeaponSocketName;
		}
	}

	return WeaponSocketName;
}



FGameplayTag UProject_JWarriorComponent::GetEffectivePrimaryAttackAbilityTag() const
{
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = GetCurrentWeaponAnimProfile())
	{
		if (WeaponAnimProfile->PrimaryAttackSpec.AbilityTag.IsValid())
		{
			return WeaponAnimProfile->PrimaryAttackSpec.AbilityTag;
		}
	}

	return PrimaryAttackAbilityTag;
}


