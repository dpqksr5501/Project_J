// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JCharacterAnimProfile.generated.h"

class UProject_JLocomotionProfile;
class UProject_JWeaponAnimProfile;
class UProject_JCombatAnimProfile;

/**
 * Top-level animation profile for a playable character archetype.
 *
 * This intentionally starts with locomotion only. Combat, weapon, and class-specific animation
 * profiles can be added here later without forcing the stable locomotion profile to own them.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UProject_JLocomotionProfile> LocomotionProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UProject_JWeaponAnimProfile> WeaponAnimProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UProject_JCombatAnimProfile> CombatAnimProfile = nullptr;
};
