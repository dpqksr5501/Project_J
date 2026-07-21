// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatTypes.h"
#include "Engine/DataAsset.h"
#include "Project_JWeaponAnimProfile.generated.h"

class UAnimInstance;
class UAnimMontage;

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

	/**
	 * Selects whether this weapon preserves the shared lower-body Motion Matching
	 * and contributes an armed upper-body pose, or supplies a validated full-body
	 * combat locomotion set. Defaulting to overlay keeps incomplete weapon assets
	 * from replacing stable shared locomotion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat|Presentation")
	EProject_JCombatAnimationPresentationMode CombatPresentationMode = EProject_JCombatAnimationPresentationMode::UpperBodyOverlay;

	/** The full-body draw / combat-entry montage for this weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatIntroMontage = nullptr;

	/**
	 * Optional weapon-specific sheathe / combat-exit montage. It is authored
	 * beside the draw montage because it belongs to the equipped weapon style,
	 * not to the job's shared locomotion graph.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UAnimMontage> CombatOutroMontage = nullptr;

	/**
	 * Weapon/job-specific Linked Anim Layer class. It overrides the production
	 * humanoid master only while this weapon is in combat mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat|Layers")
	TSoftClassPtr<UAnimInstance> CombatAnimationLayerClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CombatIntroMontagePlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CombatOutroMontagePlayRate = 1.0f;

};
