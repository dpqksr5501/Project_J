// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatTypes.h"
#include "Engine/DataAsset.h"
#include "Project_JWeaponAnimProfile.generated.h"

class AActor;
class UAnimInstance;
class UAnimMontage;
class UProject_JComboDefinition;

UENUM(BlueprintType)
enum class EProject_JWeaponAnimStance : uint8
{
	None,
	OneHanded,
	TwoHanded,
	DualWield,
	Staff,
	Bow,
	Unarmed
};

/**
 * Weapon-specific animation data.
 *
 * This asset is intentionally data-only. Empty fields are valid so characters can use the
 * shared locomotion profile before weapon and combat animations are authored.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JWeaponAnimProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EProject_JWeaponType WeaponType = EProject_JWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EProject_JWeaponAnimStance WeaponStance = EProject_JWeaponAnimStance::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AActor> WeaponActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName WeaponSocketName = NAME_None;

	/** The full-body draw / combat-entry montage for this weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatIntroMontage = nullptr;

	/**
	 * Weapon/job-specific Linked Anim Layer class. It overrides the production
	 * humanoid master only while this weapon is in combat mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat|Layers")
	TSoftClassPtr<UAnimInstance> CombatAnimationLayerClass;

	/** Immutable input graph used by the generic weapon combo Gameplay Ability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Combo")
	TObjectPtr<UProject_JComboDefinition> ComboDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CombatIntroMontagePlayRate = 1.0f;

};
