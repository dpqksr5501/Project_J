#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JWeaponPresentationProfile.generated.h"

class AActor;

/**
 * Immutable visual definition for an equipped weapon.
 *
 * Combat rules and animation selection deliberately live in CombatStyle and
 * WeaponAnimProfile. This asset may therefore vary per weapon skin without
 * duplicating a job's combat data.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JWeaponPresentationProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Version", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AActor> WeaponActorClass;

	/** Socket used while the weapon is drawn and combat presentation is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName DrawnSocketName = TEXT("WeaponSocket_R");

	/** Socket used after the weapon's sheathe montage transfers it to the back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName SheathedSocketName = TEXT("WeaponSocket_Back");

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
