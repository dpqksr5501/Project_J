#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "GameplayTagContainer.h"
#include "Project_JWeaponPresentationComponent.generated.h"

class UProject_JWeaponPresentationProfile;
class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UProject_JAttackDefinition;
class UProject_JCombatStyleDefinition;
class UAnimInstance;

/** Resolved cosmetic mount. The same endpoint is used for draw, motion return and recovery. */
struct FProject_JResolvedWeaponAttachment
{
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	FName Socket = NAME_None;
	FTransform Relative = FTransform::Identity;
	bool bPrimaryContact = false;
	/** Value-only diagnosis of the selected path; never used to drive animation. */
	FName Reason = TEXT("Unresolved");
	bool IsValid() const { return Mesh.IsValid() && !Socket.IsNone(); }
};

/** The stable character socket that currently owns the visual weapon actor. */
UENUM(BlueprintType)
enum class EProject_JWeaponPresentationSocket : uint8
{
	Sheathed UMETA(DisplayName = "Sheathed / Back"),
	Drawn UMETA(DisplayName = "Drawn / Hand")
};

/** Identifies which transform owns the weapon during the current cosmetic pose. */
UENUM(BlueprintType)
enum class EProject_JWeaponGripDriveMode : uint8
{
	BodySocket,
	PrimaryHand,
	AuthoredWeaponMotion,
	ContactRecovery
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	EProject_JWeaponGripDriveMode DriveMode = EProject_JWeaponGripDriveMode::BodySocket;

	/** Valid when the visible primary hand drives the weapon; this transform is stable across hand animation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	FTransform SecondaryGripInPrimaryHandSpace = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	FName PrimaryHandBoneName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	bool bHasPrimaryHandSpaceGrip = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	bool bPrimaryIKSuppressedByAttachment = false;

	/** Recovery owns its alpha envelope and frozen target; montage curves cannot reopen the attachment loop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon Motion")
	bool bContactRecovery = false;
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

	/** Non-player presentation adapter. Players continue to use their equipped configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Weapon")
	TObjectPtr<UProject_JWeaponPresentationProfile> OwnerPresentationProfile = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Weapon")
	TObjectPtr<UProject_JCombatStyleDefinition> PresentationCombatStyle = nullptr;

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

	/** Call after a runtime body/profile or weapon child-transform change. Active attacks finish before remounting. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefreshAttachmentCalibration();
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

	/** A notify relinquishes its temporary motion override; the attack default may continue. */
	void EndNotifyIndependentMotion();
	/** Native notify leases prevent an outgoing montage from ending a newer override. */
	uint64 BeginNotifyIndependentMotion(const TArray<FProject_JWeaponMotionKey>& MotionKeys, float PrimaryAlpha,
		float SecondaryAlpha, float Duration, float EntryBlend, float ExitBlend);
	bool IsNotifyIndependentMotionCurrent(uint64 Token) const;
	void EndNotifyIndependentMotion(uint64 Token);

	/** Event-driven cosmetic attack identity, supplied by the replicated combat presentation state. */
	void SetActiveAttackPresentation(FGameplayTag AttackTag);

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

	/**
	 * Activates two-handed weapon grip during an attack swing or skill.
	 * Legacy Blueprint calls use a balanced LIFO stack. Authored notifies use their engine instance ID.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	void BeginTwoHandGrip(float SecondaryIKAlpha = 1.0f, float PrimaryIKAlpha = 1.0f, bool bOverridePrimaryIK = false);

	/**
	 * Ends the most recent legacy Blueprint request without consuming a keyed montage notify.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	void EndTwoHandGrip();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon Motion")
	bool IsTwoHandGripActive() const { return !TwoHandGripRequests.IsEmpty(); }

	/** Per-playback identity prevents a late/duplicate end from removing another attack's grip window. */
	void BeginTwoHandGripNotify(int32 NotifyInstanceID, float SecondaryIKAlpha, float PrimaryIKAlpha, bool bOverridePrimaryIK);
	void EndTwoHandGripNotify(int32 NotifyInstanceID);

	/** Master ABPs read this once per animation update and feed the transforms to their generic hand IK nodes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon Motion")
	FProject_JWeaponGripTargets GetWeaponGripTargets();

	/** The follower pulls the current authored weapon pose before sampling IK; the late cosmetic tick remains a fallback. */
	FProject_JWeaponGripTargets GetWeaponGripTargetsForAnimation(float DeltaSeconds);

	/** Lets hit-notifies trace the rendered weapon instead of a stale character hand socket. */
	UFUNCTION(BlueprintPure, Category = "Combat|Weapon Motion")
	bool GetWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const;

	/** Returns the visual component that owns a weapon-local socket for attached cosmetic effects. */
	USceneComponent* GetWeaponVFXAttachmentComponent(FName SocketName) const;

	/** Registers a retarget anim instance for event-driven weapon target push. */
	void RegisterRetargetAnimInstance(class UProject_JRetargetAnimInstance* InAnimInstance);

	/** Unregisters a retarget anim instance on teardown. */
	void UnregisterRetargetAnimInstance(class UProject_JRetargetAnimInstance* InAnimInstance);

private:
	friend class FProjectJWeaponPresentationTeardownTest;
	friend class FProjectJWeaponPresentationIdentityTest;
	friend class FProjectJCrowdPresentationTest;
	friend class FProjectJStableGripTargetsTest;
	friend class FProjectJPresentationMeshResolverTest;
	friend class FProjectJTwoHandIKTransitionAndCurveTest;
	friend class FProjectJCanonicalMeleeTraceTest;
	friend class FProjectJPrimaryGripAttachmentTest;
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
	void UpdateContactRecovery(double NowSeconds);
	void CancelContactRecovery();
	void RefreshAttackMotion();
	bool GetAutoAttackMotionSettings(float& OutPrimaryAlpha, float& OutSecondaryAlpha);
	void UpdateGripTargets();
	bool FindWeaponSocketTransform(FName SocketName, FTransform& OutWorldTransform) const;
	USceneComponent* FindWeaponSocketComponent(FName SocketName) const;
	bool TryGetGroundCorrection(float DeltaTime, FVector& OutComponentSpaceCorrection);
	FProject_JResolvedWeaponAttachment ResolveAttachment(bool bDrawn) const;
	bool AttachWeaponToSocket(FName SourceSocketName, FName VisualSocketName, const TCHAR* Context, bool bDrawn = false);
	void DestroyWeaponPresentation();
	void UpdateTickState();
	void LogWeaponPresentationDebug(const TCHAR* Context) const;
	void LogGripTraceEvent(const TCHAR* Event) const;
	void LogAttachmentTrace(const TCHAR* Context, bool bForce = false, int32 RequestedSocket = INDEX_NONE) const;
	void SampleGripTrace();
	void NotifyWeaponTargetChanged(USceneComponent* InWeaponComponent);
	void UpdateSocketComponentCache();
	void InvalidateSocketComponentCache();

	FName CachedPrimaryGripSocket = NAME_None;
	FName CachedSecondaryGripSocket = NAME_None;
	/** Also caches ground probes and cosmetic VFX sockets, including absent names. */
	mutable TMap<FName, TWeakObjectPtr<USceneComponent>> CachedSocketComponents;
	mutable TSet<FName> MissingSocketNames;

	TArray<TWeakObjectPtr<class UProject_JRetargetAnimInstance>> RegisteredRetargetAnimInstances;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Combat|Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> SpawnedWeapon = nullptr;

	TWeakObjectPtr<const UProject_JWeaponPresentationProfile> AppliedProfile;
	TWeakObjectPtr<UClass> AppliedActorClass;
	TWeakObjectPtr<USkeletalMeshComponent> AppliedCharacterMesh;
	TWeakObjectPtr<USkeletalMesh> AppliedSkeletalMesh;

	FVector SmoothedGroundCorrectionComponentSpace = FVector::ZeroVector;
	FProject_JWeaponGripTargets GripTargets;

	TArray<FProject_JWeaponMotionKey> ActiveMotionKeys;
	/** Fixed entry pose: never read the IK-driven visual hand while blending to the source arc. */
	FTransform ActiveMotionEntryWorld = FTransform::Identity;
	FGameplayTag ActiveAttackPresentationTag;
	FGameplayTag ResolvedAttackPresentationTag;
	TWeakObjectPtr<const UProject_JCombatStyleDefinition> ResolvedAttackStyle;
	TWeakObjectPtr<const UProject_JAttackDefinition> ResolvedAttackDefinition;
	bool bNotifyOwnsMotion = false;
	uint64 NextNotifyMotionToken = 0;
	uint64 ActiveNotifyMotionToken = 0;
	bool bAutoAttackMotionActive = false;
	TWeakObjectPtr<UAnimInstance> AutoAttackSourceAnim;
	int32 AutoAttackSourceInstanceID = INDEX_NONE;
	/** Cosmetic socket to return to after source-space motion keys finish. No replication. */
	TWeakObjectPtr<USkeletalMeshComponent> ActiveMotionReturnMesh;
	FName ActiveMotionReturnSocket = NAME_None;
	FTransform ActiveMotionReturnRelative = FTransform::Identity;

	float ActiveMotionNormalizedTime = 0.0f;
	float ActiveMotionDurationSeconds = 0.0f;
	float ActiveEntryBlendSeconds = 0.0f;
	float ActiveExitBlendSeconds = 0.0f;
	float ActivePrimaryGripIKAlpha = 0.0f;
	float ActiveSecondaryGripIKAlpha = 0.0f;

	/** Independent of the solved hand: captured component-space contact and socket offset. */
	TWeakObjectPtr<USkeletalMeshComponent> ContactRecoveryMesh;
	FName ContactRecoverySocket = NAME_None;
	FTransform ContactRecoveryGripComponent = FTransform::Identity;
	FTransform ContactRecoveryAttachment = FTransform::Identity;
	FTransform ContactRecoveryDestination = FTransform::Identity;
	bool bPrimaryContactAttachment = false;
	bool bAttachmentRefreshRequested = false;
	/** Source-driven recovery reads an independent source pose, never the solved primary hand. */
	TWeakObjectPtr<USkeletalMeshComponent> ContactRecoverySourceMesh;
	TWeakObjectPtr<UAnimInstance> ContactRecoverySourceAnim;
	FName ContactRecoverySourceSocket = NAME_None;
	int32 ContactRecoverySourceInstanceID = INDEX_NONE;
	float ContactRecoverySourceStartWeight = 0.0f;
	FTransform ContactRecoveryWeaponInSourceSocket = FTransform::Identity;
	FTransform ContactRecoveryGripInSourceSocket = FTransform::Identity;
	double ContactRecoveryStartSeconds = 0.0;
	float ContactRecoveryDurationSeconds = 0.0f;
	float ContactRecoveryPrimaryAlpha = 0.0f;
	float ContactRecoverySecondaryAlpha = 0.0f;
	float ContactRecoveryAlpha = 0.0f;
	bool bContactRecoveryActive = false;
	uint64 LastMotionEvaluationFrame = MAX_uint64;
	int32 GroundContactStateCount = 0;
	struct FTwoHandGripRequest
	{
		int32 NotifyInstanceID = INDEX_NONE;
		float SecondaryAlpha = 1.0f;
		float PrimaryAlpha = 1.0f;
		bool bOverridePrimary = false;
	};
	/** Latest surviving window wins; endings remove only their own request. No state is stored on shared notify assets. */
	TArray<FTwoHandGripRequest, TInlineAllocator<4>> TwoHandGripRequests;

	float WeaponPresentationDebugElapsedSeconds = 0.0f;
	double GripTraceNextSampleTime = 0.0;
	mutable double AttachmentTraceNextSampleTime = 0.0;
	double GripTracePreviousSampleTime = 0.0;
	FVector GripTracePreviousWeapon = FVector::ZeroVector;
	FVector GripTracePreviousElbow = FVector::ZeroVector;
	FVector GripTracePreviousElbowPlane = FVector::ZeroVector;
	bool bGripTraceHasPreviousSample = false;
	bool bCombatPresentationActive = false;
	EProject_JWeaponPresentationSocket CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	bool bIndependentMotionActive = false;
	bool bEndingPlay = false;
	bool bRefreshingPresentation = false;
};
