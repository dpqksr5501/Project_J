#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "Project_JWeaponPresentationComponent.generated.h"

class UProject_JWeaponPresentationProfile;
class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;

/** The stable character socket that currently owns the visual weapon actor. */
UENUM(BlueprintType)
enum class EProject_JWeaponPresentationSocket : uint8
{
	Sheathed UMETA(DisplayName = "Sheathed / Back"),
	Drawn UMETA(DisplayName = "Drawn / Hand")
};

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
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Ensures the equipped weapon is available for a draw transition, initially at its sheathed socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void EnterCombatPresentation();

	/** Ends combat presentation and normalizes the visual weapon to its sheathed socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void ExitCombatPresentation();

	/** Keeps the drawn weapon alive while a sheathe montage is playing. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void BeginSheathePresentation();

	/** Attaches the visible weapon to the authored back/sheath socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AttachWeaponToSheathedSocket();

	/** Attaches the visible weapon to the authored combat/hand socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AttachWeaponToDrawnSocket();

	/**
	 * Shared montage-notify entry point. It moves the same weapon actor between
	 * profile-authored sockets; it never changes authoritative equipment data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void SetWeaponPresentationSocket(EProject_JWeaponPresentationSocket Socket);

	/** Reconciles the visible weapon; unchanged identity preserves its actor and motion. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefreshPresentation();
	/** Budget subsystem applies only the current request; no stale weapon profile is captured. */
	void ApplyBudgetedPresentation(uint64 Revision);

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

	/** Returns the visual component that owns a weapon-local socket for attached cosmetic effects. */
	USceneComponent* GetWeaponVFXAttachmentComponent(FName SocketName) const;

private:
	friend class FProjectJWeaponPresentationTeardownTest;
	friend class FProjectJWeaponPresentationIdentityTest;
	friend class FProjectJCrowdPresentationTest;
	bool ShouldBudgetPresentation() const;
	void CancelBudgetedPresentation();
	uint64 PresentationRevision = 0;
	bool bApplyingBudget = false, bRetryBudget = false;
	double NextBudgetRetry = 0;
#if WITH_DEV_AUTOMATION_TESTS
	bool bForceBudgetForTest = false;
#endif
	bool CanCreatePresentation() const;
	const UProject_JWeaponPresentationProfile* GetCurrentPresentationProfile() const;
	bool ShouldShowWeapon() const;
	void UpdateIndependentMotion(float DeltaTime);
	void UpdateGripTargets();
	bool FindWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const;
	bool TryGetGroundCorrection(float DeltaTime, FVector& OutComponentSpaceCorrection);
	bool AttachWeaponToSocket(FName SocketName, const TCHAR* Context);
	void DestroyWeaponPresentation();
	void UpdateTickState();
	void LogWeaponPresentationDebug(const TCHAR* Context) const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Combat|Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> SpawnedWeapon = nullptr;

	TWeakObjectPtr<const UProject_JWeaponPresentationProfile> AppliedProfile;
	TWeakObjectPtr<UClass> AppliedActorClass;
	TWeakObjectPtr<USkeletalMeshComponent> AppliedCharacterMesh;
	TWeakObjectPtr<USkeletalMesh> AppliedSkeletalMesh;

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
	EProject_JWeaponPresentationSocket CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	bool bIndependentMotionActive = false;
	bool bEndingPlay = false;
	bool bRefreshingPresentation = false;
};
