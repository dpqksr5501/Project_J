#pragma once

#include "CoreMinimal.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Engine/DataAsset.h"
#include "Project_JStatTypes.h"
#include "Project_JEquipmentItemDefinition.generated.h"

class USkeletalMesh;
class UGameplayAbility;
class UGameplayEffect;

UENUM(BlueprintType)
enum class EProject_JEquipmentStatApplicationPolicy : uint8
{
	GameplayEffectsThenStatModifiers,
	GameplayEffectsOnly,
	StatModifiersOnly
};

/**
 * Data-driven definition for an equipment piece (Armor, Weapon, Accessory).
 * Allows designers to create equipment items in the editor without writing C++.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JEquipmentItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Logical slot used to enforce one equipped item per gameplay slot.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment")
	EProject_JEquipmentSlot EquipmentSlot = EProject_JEquipmentSlot::Weapon;

	// The skeletal mesh representing the equipment.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual")
	TSoftObjectPtr<USkeletalMesh> EquipmentMesh;

	// Socket name to attach to. If empty, it assumes standard body armor attaching (Leader Pose).
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual")
	FName AttachSocketName;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxDrawDistance = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual|Optimization")
	bool bCastDynamicShadow = true;

	// AbilitySet containing skills and passives granted when this equipment is equipped.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Abilities")
	TObjectPtr<class UProject_JAbilitySet> AbilitySet;

	// GameplayEffects applied while this item is equipped.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Effects")
	TArray<TSubclassOf<UGameplayEffect>> EquipmentEffects;

	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Effects")
	EProject_JEquipmentStatApplicationPolicy StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::GameplayEffectsThenStatModifiers;

	// Fixed attribute bonuses applied while this item is equipped.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Stats")
	TArray<FProject_JEquipmentStatModifier> StatModifiers;

	// The Weapon Animation Profile (Motion Matching DBs & Montages) associated with this equipment.
	// We use the existing UProject_JWeaponAnimProfile instead of a duplicate system.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Animation")
	class UProject_JWeaponAnimProfile* WeaponAnimProfile;
};
