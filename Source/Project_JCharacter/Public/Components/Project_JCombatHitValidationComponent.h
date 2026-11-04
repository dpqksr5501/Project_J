#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatHitValidationComponent.generated.h"

class UAbilitySystemComponent;
class UProject_JAttackDefinition;
struct FAnimNotifyEventReference;

/** One actual server-produced piece; client RPC endpoints never supply this path. */
USTRUCT()
struct FProject_JAuthoritativeTraceSegment
{
	GENERATED_BODY()
	UPROPERTY() FVector TraceStart = FVector::ZeroVector;
	UPROPERTY() FVector TraceEnd = FVector::ZeroVector;
};

/** Records one authoritative native dispatch at a server timestamp, never one slot per path piece. */
USTRUCT()
struct FProject_JAuthoritativeSweepRecord
{
	GENERATED_BODY()

	UPROPERTY()
	double ServerTimestamp = 0.0;

	UPROPERTY()
	FVector TraceStart = FVector::ZeroVector;

	UPROPERTY()
	FVector TraceEnd = FVector::ZeroVector;

	UPROPERTY()
	FGameplayTag AttackNodeTag;

	UPROPERTY()
	int32 PredictionKey = 0;

	UPROPERTY()
	bool bHitWindowOpen = false;

	/** Legacy records keep their two-vector interpolation. Atomic paths are selected whole. */
	UPROPERTY() bool bAtomicPath = false;
	UPROPERTY() TArray<FProject_JAuthoritativeTraceSegment> Segments;
	UPROPERTY() uint64 PacketSerial = 0;
	UPROPERTY() uint64 WindowToken = 0;
	UPROPERTY() uint64 SpatialRevision = 0;
};

/** Shared server-authoritative melee hit validation for all player jobs. */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatHitValidationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatHitValidationComponent();

	/** Called by the authoritative/predicted melee ability when a new combo node starts. */
	void BeginAttackNode(FGameplayTag AttackNodeTag, UProject_JAttackDefinition* AttackDefinition, int32 PredictionKey = 0);
	void EndAttack();
	/** Retire pre-teleport geometry while preserving attack identity, windows and duplicate suppression. */
	void ResetSpatialHistory();
	uint64 GetSpatialRevision() const { return SpatialRevision; }
	/** Previous real finalized world pose for one direct queued montage event.
	 * False is unavailable/unsupported/discontinuous, never an invented pose. */
	bool TryGetPreviousMeleePose(class USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference,
		FVector& OutWorldPoint, float& OutMontageTime) const;
	/** Actual committed root endpoints for an explicit canonical mesh-local path.
	 * Translation lerp / quaternion slerp is the canonical policy, not a recovered physics path.
	 * False leaves outputs unchanged; unsupported roots, warping or discontinuities fail closed. */
	bool TryGetMeleeRootInterval(class USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference,
		FTransform& OutPreviousRoot, FTransform& OutCurrentRoot, FTransform& OutMeshToRoot, float& OutPreviousMontageTime) const;
	void SetHitWindowOpen(bool bOpen);
	/** A window token belongs to the current attack node; node replacement invalidates every old token. */
	uint64 BeginHitWindow();
	void EndHitWindow(uint64 Token);
	bool IsHitWindowCurrent(uint64 Token) const { return Token != 0 && HitWindowTokens.Contains(Token); }

	/**
	 * Records the server's authored weapon sweep for the active notify window.
	 * Client traces remain useful as a lag-compensation hint, but never become
	 * authoritative damage geometry.
	 */
	void RecordAuthoritativeTrace(const FVector& TraceStart, const FVector& TraceEnd);
	static constexpr int32 MaxTracePacketSegments = 63;
	static constexpr int32 MaxRetainedTraceSegments = 192;
	/** GT-only current-world LOS query; no admission, damage, loading or state mutation. */
	bool IsTargetObstructedFrom(const AActor* HitActor, const FVector& TraceOrigin) const;
	/** Atomically record the completed native query path. Zero rejects without partial publication. */
	uint64 RecordAuthoritativeTracePacket(TConstArrayView<FProject_JAuthoritativeTraceSegment> Segments,
		uint64 WindowToken, uint64 ExpectedSpatialRevision);
	/** Select the exact contact's piece before the existing authority event; no additional history write. */
	bool SelectAuthoritativeTraceSegment(uint64 PacketSerial, uint64 WindowToken,
		uint64 ExpectedSpatialRevision, int32 SegmentIndex);
	/** Owning value result; legacy lookup materializes one piece, atomic geometry is never endpoint-interpolated. */
	bool FindAuthoritativeTracePacketAtTime(double TargetTimestamp, int32 ExpectedPredictionKey,
		const FGameplayTag& ExpectedAttackNodeTag, FProject_JAuthoritativeSweepRecord& OutPacket) const;

	/** Local predicted client path. Sequence is generated internally and verified by the server. */
	void SubmitPredictedHit(AActor* HitActor, double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd);

	/** Cheap GT admission only; final authority/geometry checks remain in ProcessAuthorityHit. No callbacks. */
	bool CanProcessAuthorityHit(AActor* HitActor, int32 TargetLimit = MAX_int32) const;
	/** Authority/AI path. A finite limit uses the existing per-attack dedup set, reserving a slot before effect callbacks. */
	bool ProcessAuthorityHit(AActor* HitActor, int32 TargetLimit = MAX_int32);

	FGameplayTag GetActiveAttackNodeTag() const { return ActiveAttackNodeTag; }
	const UProject_JAttackDefinition* GetActiveAttackDefinition() const { return ActiveAttackDefinition.Get(); }
	/** Actual BeginAttackNode clock on this role; End reports ordinal zero. */
	double GetActiveAttackStartTimestamp() const { return ActiveAttackStartTimestamp; }
	int32 GetActiveNodeVisitOrdinal() const { return ActiveNodeVisitOrdinal; }

	UFUNCTION(Server, Unreliable)
	void ServerRequestSSRHit(AActor* HitActor, double ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence, int32 PredictionKey);

	/** Canonical prediction requires this explicit clock; no legacy downgrade. */
	UFUNCTION(Server, Unreliable)
	void ServerRequestPredictedHit(FProject_JCombatHitRequest Request);

	FProject_JCombatHitValidationResult ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const;

	/** Legacy single-segment interpolation only. Atomic paths return false with cleared outputs; use packet lookup. */
	bool FindAuthoritativeTraceAtTime(double TargetTimestamp, int32 ExpectedPredictionKey, const FGameplayTag& ExpectedAttackNodeTag, FVector& OutStart, FVector& OutEnd, bool& OutHitWindowOpen) const;

	/** Convenience overload matching active state. */
	bool FindAuthoritativeTraceAtTime(double TargetTimestamp, FVector& OutStart, FVector& OutEnd) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void ProtectAttackPose();
	void RestoreAttackPose();
	TWeakObjectPtr<class USkeletalMeshComponent> ProtectedMesh;
	struct FMeleePoseSample
	{
		TWeakObjectPtr<class UProject_JAnimNotifyState_MeleeHit> Notify;
		FName Socket;
		FVector TipOffset=FVector::ZeroVector, WorldPoint=FVector::ZeroVector;
		float MontageTime=0, TriggerTime=0, EndTriggerTime=0;
		int32 NotifyIndex=INDEX_NONE;
		uint64 Frame=MAX_uint64, Spatial=0;
		uint32 BoneRevision=0;
		bool bUseTipOffset=false, bValid=false;
	};
	TArray<FMeleePoseSample> MeleePoseSamples; // At most64 authored notify points, active attack only.
	struct FMeleeRootAnchor
	{
		TWeakObjectPtr<class UCharacterMoverComponent> Mover;
		TWeakObjectPtr<class USceneComponent> Root;
		FTransform RootWorld=FTransform::Identity, MeshToRoot=FTransform::Identity;
		TWeakObjectPtr<class UPrimitiveComponent> MovementBase;
		FName BaseBone=NAME_None;
		FTransform CapturedBaseFrame=FTransform::Identity;
		bool bBased=false;
		uint64 Frame=MAX_uint64, Spatial=0;
		bool bValid=false;
	};
	FMeleeRootAnchor MeleeRootAnchor; // One common anchor per finalized pose, not64 transform copies.
	bool ReadMeleeRootAnchor(class USkeletalMeshComponent* Mesh, FMeleeRootAnchor& OutAnchor) const;
	const FMeleePoseSample* FindPreviousMeleePoseSample(class USkeletalMeshComponent* Mesh, const FAnimNotifyEventReference& EventReference) const;
	TWeakObjectPtr<class USkeletalMesh> MeleePoseAsset;
	TWeakObjectPtr<class UAnimInstance> MeleePoseAnim;
	TWeakObjectPtr<UClass> MeleePoseAnimClass;
	TWeakObjectPtr<class UAnimMontage> MeleePoseMontage;
	FDelegateHandle MeleePoseHandle;
	uint64 MeleePoseGeneration=0;
	int32 MeleePoseInstanceID=INDEX_NONE;
	void BeginMeleePoseCapture();
	void EndMeleePoseCapture();
	void CaptureMeleePose(uint64 ExpectedGeneration);
	uint8 SavedVisibility = 0;
	bool bSavedURO = false;
	bool bSavedSuppressNotifies = false;
	friend class FProjectJWeaponAttackLifetimeTest;
	friend class FProjectJSSRHistoricalSweepTest;
	friend class FProjectJSSRHitWindowProductionTransitionTest;
	bool HasValidAttackWeapon() const;
	TWeakObjectPtr<class UProject_JEquipmentRuntimeComponent> AttackEquipment;
	uint64 AttackWeaponRevision = 0;
	int32 ActivePredictionKey = 0;
	uint64 SpatialRevision = 0;
	double ActiveAttackStartTimestamp = 0.0;
	int32 ActiveNodeVisitOrdinal = 0;
	// One last activation provenance, retained through End so a same-key restart
	// cannot reopen visit one. Different keys begin at one; no unbounded key map.
	int32 LastNodePredictionKey = 0;
	int32 LastNodeVisitOrdinal = 0;
	bool bClientNodeClockBound = false;
	double BoundClientNodeStartTimestamp = 0.0;
	bool bNodeClockRetired = false;
	void ProcessServerHitRequest(const FProject_JCombatHitRequest& Request);
	EProject_JCombatHitValidationFailure ResolveSourceLookupTimestamp(
		const FProject_JCombatHitRequest& Request, double& OutTimestamp) const;
	bool bRequiresWeapon = false;
	UAbilitySystemComponent* ResolveOwnerAbilitySystemComponent() const;
	bool ApplyConfirmedHit(AActor* HitActor, TFunctionRef<bool()> IsCurrent);
	EProject_JCombatHitValidationFailure ValidateActiveAttack(const FProject_JCombatHitRequest& Request, bool bCheckLiveTraceLOS = true);
	bool IsTargetObstructed(const AActor* HitActor) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|SSR", meta = (AllowPrivateAccess = "true"))
	FProject_JCombatHitValidationPolicy HitValidationPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JAttackDefinition> ActiveAttackDefinition = nullptr;

	FGameplayTag ActiveAttackNodeTag;
	TSet<TWeakObjectPtr<const AActor>> ServerHitActors;
	bool bHitWindowOpen = false;
	bool bManualHitWindowOpen = false;
	uint64 NextHitWindowToken = 0;
	TSet<uint64> HitWindowTokens;
	void ApplyHitWindowState(bool bOpen);
	FVector LastAuthoritativeTraceStart = FVector::ZeroVector;
	FVector LastAuthoritativeTraceEnd = FVector::ZeroVector;
	bool bHasAuthoritativeTrace = false;

	/** Authoritative weapon sweep ringbuffer indexed over recent server timestamps for lag compensation. */
	UPROPERTY(Transient)
	TArray<FProject_JAuthoritativeSweepRecord> AuthoritativeSweepHistory;

	int32 SweepHistoryStartIndex = 0;
	int32 SweepHistoryCount = 0;
	int32 SweepHistorySegmentCount = 0;
	uint64 NextTracePacketSerial = 0;
	/** Fixed record cap plus a shared geometry cap; expiration/eviction retires oldest whole packets. */
	static constexpr int32 MaxSweepHistoryCapacity = 128;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|SSR", meta = (ClampMin = "0.1", Units = "s"))
	float MaxSweepHistorySeconds = 1.5f;

	const FProject_JAuthoritativeSweepRecord& GetSweepHistoryRecord(int32 LogicalIndex) const;
	void AppendSweepHistoryRecord(const FProject_JAuthoritativeSweepRecord& Record);
	void RetireOldestSweepRecord();
	void ResetSweepHistory();
	int32 FindExactAtomicPublication(double TargetTimestamp) const;
	void AppendCurrentAuthoritativeSweepState();
	void DiscardExpiredSweepRecords(double CurrentTimestamp);

	int32 LocalRequestSequence = 0;
	int32 LastServerRequestSequence = 0;
	double RateWindowStartSeconds = 0.0;
	int32 RequestsInRateWindow = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|SSR", meta = (ClampMin = "1", ClampMax = "120"))
	int32 MaxRequestsPerSecond = 32;
};
