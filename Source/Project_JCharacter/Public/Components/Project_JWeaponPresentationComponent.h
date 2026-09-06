#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "Project_JWeaponPresentationComponent.generated.h"

class UProject_JWeaponPresentationProfile;
class USceneComponent;

/** Runtime IK values exposed to the shared Master ABP. Values are cosmetic and intentionally not replicated. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JWeaponGripTargets
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	FTransform PrimaryGripWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	FTransform SecondaryGripWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	float PrimaryIKAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	float SecondaryIKAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	bool bHasPrimaryGrip = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	bool bHasSecondaryGrip = false;
};

/**
 * Owns the runtime weapon actor shared by every humanoid job.
 *
 * Equipped-item data selects the actor and socket through WeaponPresentationProfile. Individual
 * job characters therefore do not need a job-named weapon component merely to
 * display their weapon.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JWeaponPresentationComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Shows the currently selected weapon in its authored combat socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void EnterCombatPresentation();

	/** Removes the runtime weapon actor. Draw/sheath notifies can replace this policy later. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void ExitCombatPresentation();

	/** Keeps the drawn weapon alive while a sheathe montage is playing. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void BeginSheathePresentation();

	/** Attaches the visible weapon to the authored back/sheath socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AttachWeaponToSheathedSocket();

	/** Rebuilds the visible weapon when the effective weapon profile changes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefreshPresentation();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	AActor* GetSpawnedWeapon() const { return SpawnedWeapon; }

	/**
	 * Starts the unified transform keys embedded in a Weapon Motion Montage
	 * notify. This is purely cosmetic: gameplay replication remains driven by
	 * the ability/montage that owns the notify.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	bool BeginIndependentMotion(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	void EndIndependentMotion();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon Motion")
	bool IsIndependentMotionActive() const { return bIndependentMotionActive; }

	/** Called by the montage notify state with its normalized local play position. */
	void SetIndependentMotionPosition(float NormalizedTime);

	/** Refreshes keys if an artist changes the selected Montage notify while it is previewing. */
	void RefreshIndependentMotionKeys(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryGripIKAlpha, float SecondaryGripIKAlpha, float MotionDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds);

	/** A separate montage state enables ground correction only over the actual dragging interval. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	void BeginGroundContact();

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	void EndGroundContact();

	/** Master ABPs read this once per animation update and feed the transforms to their generic hand IK nodes. */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon Motion")
	FProject_JWeaponGripTargets GetWeaponGripTargets() const { return GripTargets; }

	/** Lets hit-notifies trace the rendered weapon instead of a stale character hand socket. */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon Motion")
	bool GetWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const;

private:
	const UProject_JWeaponPresentationProfile* GetCurrentPresentationProfile() const;
	bool ShouldShowWeapon() const;
	void UpdateIndependentMotion(float DeltaTime);
	void UpdateGripTargets();
	bool FindWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const;
	bool TryGetGroundCorrection(float DeltaTime, FVector& OutComponentSpaceCorrection);
	void AttachWeaponToDrawnSocket();
	void UpdateTickState();
	void LogWeaponPresentationDebug(const TCHAR* Context) const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Combat|Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> SpawnedWeapon = nullptr;

	FVector SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	FProject_JWeaponGripTargets GripTargets;

	TArray<FProject_JWeaponMotionKey> ActiveMotionKeys;

	float ActiveMotionNormalizedTime = 0.0f;
	float ActiveMotionDurationSeconds = 0.0f;
	float ActiveEntryBlendSeconds = 0.0f;
	float ActiveExitBlendSeconds = 0.0f;
	float ActivePrimaryGripIKAlpha = 0.0f;
	float ActiveSecondaryGripIKAlpha = 0.0f;
	int32 GroundContactStateCount = 0;

	float WeaponPresentationDebugElapsedSeconds = 0.0f;
	bool bCombatPresentationActive = false;
	bool bIndependentMotionActive = false;
};
