#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JEquipmentItemDefinition.generated.h"

class USkeletalMesh;
class UGameplayAbility;

/**
 * Data-driven definition for an equipment piece (Armor, Weapon, Accessory).
 * Allows designers to create equipment items in the editor without writing C++.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JEquipmentItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// The skeletal mesh representing the equipment.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual")
	TSoftObjectPtr<USkeletalMesh> EquipmentMesh;

	// Socket name to attach to. If empty, it assumes standard body armor attaching (Leader Pose).
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Visual")
	FName AttachSocketName;

	// List of abilities granted to the character when this equipment is equipped.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

	// The Weapon Animation Profile (Motion Matching DBs & Montages) associated with this equipment.
	// We use the existing UProject_JWeaponAnimProfile instead of a duplicate system.
	UPROPERTY(EditDefaultsOnly, Category = "Equipment|Animation")
	class UProject_JWeaponAnimProfile* WeaponAnimProfile;
};
