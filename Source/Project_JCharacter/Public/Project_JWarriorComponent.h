// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatComponent.h"
#include "Project_JWarriorComponent.generated.h"

class UAnimMontage;
class UProject_JWeaponAnimProfile;
struct FGameplayTag;
struct FProject_JWeaponAttackSpec;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JWarriorComponent : public UProject_JCombatComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UProject_JWarriorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Spawns and equips the sword actor onto the owner character */
	virtual void EquipWeapon() override;

	/** Unequips and destroys the sword actor */
	virtual void UnequipWeapon() override;

protected:
	const UProject_JWeaponAnimProfile* GetCurrentWeaponAnimProfile() const;
	TSubclassOf<AActor> GetEffectiveWeaponClass() const;
	FName GetEffectiveWeaponSocketName() const;
	FGameplayTag GetEffectivePrimaryAttackInputTag() const;
	FGameplayTag GetEffectivePrimaryAttackAbilityTag() const;

protected:
	/** Weapon Actor class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Weapon")
	TSubclassOf<AActor> WeaponClass;

	/** Socket to attach the weapon to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warrior|Weapon")
	FName WeaponSocketName = FName("WeaponSocket_R");

	/** Spawned Weapon Actor reference */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Warrior|Weapon")
	AActor* SpawnedWeapon = nullptr;
};
