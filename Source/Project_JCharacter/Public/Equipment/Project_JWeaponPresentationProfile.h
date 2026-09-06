#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JWeaponPresentationProfile.generated.h"

class AActor;

/** Data-only contact configuration. Probe sockets live on the weapon visual, never on the shared character skeleton. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JWeaponGroundContactSettings
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact")
	bool bEnableGroundContact = false;

	/** A socket placed at the authored lowest contact point of the weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact"))
	FName PrimaryProbeSocketName = TEXT("WeaponGroundProbe_Tip");

	/** Optional fallback probe. It is used only when the primary socket is absent; contact is never averaged across a blade. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact"))
	FName SecondaryProbeSocketName = TEXT("WeaponGroundProbe_Base");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact", ClampMin = "0.0"))
	float TraceStartHeight = 75.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact", ClampMin = "1.0"))
	float TraceLength = 250.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact", ClampMin = "0.0"))
	float SurfaceClearance = 0.5f;

	/** Prevents an unsuitable weapon/animation pair from being pulled across the whole scene. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact", ClampMin = "0.0"))
	float MaxTranslationCorrection = 35.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact", ClampMin = "0.0"))
	float CorrectionInterpolationSpeed = 18.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Distant/hidden characters retain their authored curve but skip collision queries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact"))
	bool bOnlyTraceWhenRecentlyRendered = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion|Ground Contact", meta = (EditCondition = "bEnableGroundContact && bOnlyTraceWhenRecentlyRendered", ClampMin = "0.0"))
	float RecentlyRenderedToleranceSeconds = 0.25f;
};

/**
 * Generic presentation data for any weapon that must temporarily move
 * independently of the primary hand. No character-skeleton extension is required.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JWeaponMotionPresentation
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion")
	bool bSupportsIndependentMotion = false;

	/** Weapon-local socket used as the primary (usually right-hand) IK target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion", meta = (EditCondition = "bSupportsIndependentMotion"))
	FName PrimaryGripSocketName = TEXT("WeaponGrip_R");

	/** Weapon-local socket used as the secondary (usually left-hand) IK target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion", meta = (EditCondition = "bSupportsIndependentMotion"))
	FName SecondaryGripSocketName = TEXT("WeaponGrip_L");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion", meta = (EditCondition = "bSupportsIndependentMotion"))
	FProject_JWeaponGroundContactSettings GroundContact;
};

/**
 * Immutable visual definition for an equipped weapon.
 *
 * Combat rules and animation selection deliberately live in CombatStyle and
 * WeaponAnimProfile. This asset may therefore vary per weapon skin without
 * duplicating a job's combat data. WeaponActorClass should derive from the
 * common AProject_JWeaponPresentationActor Blueprint base.
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

	/** Optional data-driven independent weapon motion. Kept separate from combat rules and item stats. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Motion")
	FProject_JWeaponMotionPresentation MotionPresentation;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
