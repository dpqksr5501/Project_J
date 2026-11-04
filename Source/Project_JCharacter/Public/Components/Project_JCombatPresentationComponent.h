#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatPresentationComponent.generated.h"

class UNiagaraComponent;
class ULineBatchComponent;
class UProject_JCombatPresentationSet;
class UProject_JPresentationBudgetSubsystem;
struct FProject_JCombatVFXCueDefinition;

/** Recovery for persistent optional cosmetics and producer-owned warnings; visual components never replicate. */
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JReplicatedCombatPresentationState
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag ActiveAttackTag;

	UPROPERTY()
	FGameplayTagContainer ActiveLoopingCueTags;

	UPROPERTY()
	int32 Revision = 0;
    /** Warning identity is independent of an optional, possibly cold presentation profile. */
    UPROPERTY()
    FGameplayTagContainer ActiveWarningCueTags;
	UPROPERTY()
	int32 EventOrder = 0;
	UPROPERTY()
	uint32 AttackInstance = 0;
};

/**
 * Client visual owner for optional cosmetics and a merged asset-independent baseline warning.
 * All relevant rendered clients receive warning meaning regardless of owner local control.
 * No component tick; existing compact recovery owns loops and warning start/end.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatPresentationComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	/** NPC/non-player base set. Player combat-style -> advancement -> skin precedence is unchanged. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Presentation")
	TObjectPtr<UProject_JCombatPresentationSet> BasePresentationSet;

	/** Sets the current attack identity used by montage presentation notifies. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void BeginAttackPresentation(FGameplayTag AttackTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void EndAttackPresentation();

	/** Called by a generic montage notify/state; it only affects local presentation. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void PlayCue(FGameplayTag CueTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void StopCue(FGameplayTag CueTag);

	/** Exact notify lease. Producer warning meaning is independent of optional profiles; false preserves legacy cosmetics. */
	uint64 BeginCueLease(FGameplayTag CueTag, bool bGameplayWarning = false);
	void EndCueLease(uint64 Token);
	bool IsCueLeaseCurrent(uint64 Token) const { return CueLeases.Contains(Token); }
	/** GT-only local loop observation. A current lease can remain null before visual admission. */
	TWeakObjectPtr<UNiagaraComponent> GetCueLeaseComponent(uint64 Token) const;

	/** Call after combat style, advancement, or visual weapon data changes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void RefreshPresentation();

	UFUNCTION(BlueprintPure, Category = "Combat|Presentation")
	FGameplayTag GetActiveAttackTag() const { return ActiveAttackTag; }

private:
	friend class UProject_JPresentationBudgetSubsystem;
	friend class FProjectJPresentationRecoveryTest;
    friend class FProjectJGameplayWarningTest;
	friend class FProjectJAudit3CueDeltaRecoveryTest;
	friend class AProject_JAuditNetworkController;
	friend class FProjectJCrowdEffectsTest;
	friend class FProjectJCueLeaseOwnershipTest;
	uint64 NextCueLeaseToken = 0;
	TMap<uint64, FGameplayTag> CueLeases;
    // Token purpose is immutable: a cosmetic end cannot release a warning with the same tag.
    TSet<uint64> WarningCueLeases;
    FGameplayTagContainer ActiveWarningCues;
    /** Exactly one merged, fixed-size native attention marker per owner. Never a damage-area representation. */
    UPROPERTY(Transient)
    TObjectPtr<ULineBatchComponent> WarningMarker;
    void UpdateWarningMarker();
    void EndWarningCue(FGameplayTag CueTag);
	const FProject_JCombatVFXCueDefinition* ResolveCue(FGameplayTag CueTag) const;
	void PlayCueLocal(FGameplayTag CueTag, bool bBudgetGranted = false);
	bool ShouldBudgetRemoteSpawn(const FProject_JCombatVFXCueDefinition& Cue) const;
	void StopCueLocal(FGameplayTag CueTag);
	void StopAllCues(bool bKeepWarnings = false);
	void RetirePresentation();
	bool CanAcceptPresentation() const;

	// World-local loader owns residency and work admission; this component owns
	// only its bounded leases and desired loops. No component tick is required.
	static constexpr int32 MaxVisualAssetPaths = 64;
	static constexpr int32 MaxPrefetchProfiles = 128;
	static constexpr int32 MaxPrefetchCueVisits = 256;
	struct FPendingLoop
	{
		FSoftObjectPath SystemPath;
		uint32 AttackInstance = 0;
		// Ready remote cosmetics (warm or loaded asynchronously) share the existing GT queue.
		bool bReadyForBudget = false;
	};
	TMap<FSoftObjectPath, uint64> VisualAssetLeases;
	TSet<FSoftObjectPath> PrefetchOnlyPaths;
	TMap<FGameplayTag, FPendingLoop> PendingLoopingCues;
	uint32 VisualAssetRevision = 0;
	uint32 ResetVisualAssetLeases();
	bool EnsureVisualAsset(const FSoftObjectPath& Path, bool bAtCue = false);
	void PreloadVisualAssets();
	void OnVisualAssetReady(const FSoftObjectPath& Path, uint32 Revision, UObject* Asset);
	void QueueReadyLoopApplications();
	void CancelReadyLoopApplications();
	void ApplyBudgetedReadyLoop(uint64 Revision);
	uint64 RejectedReadyLoops = 0;
	using FPresentationSets = TArray<const UProject_JCombatPresentationSet*, TInlineAllocator<3>>;
	void GatherPresentationSets(FPresentationSets& Sets) const;
	bool bEndingPresentation = false;
	void PublishRecoveryState();
	void ApplyReplicatedState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayPresentationCue(FGameplayTag AttackTag, FGameplayTag CueTag, bool bStart, int32 EventOrder, uint32 AttackInstance, const FProject_JReplicatedCombatPresentationState& RecoveryState);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEndAttackPresentation(int32 EventOrder);

    /** Transition-only reliable warning recovery; cosmetics retain their existing unreliable delta. */
    UFUNCTION(NetMulticast, Reliable)
    void MulticastWarningPresentationState(const FProject_JReplicatedCombatPresentationState& RecoveryState);

	UFUNCTION()
	void OnRep_PresentationState(FProject_JReplicatedCombatPresentationState PreviousState);

	FGameplayTag ActiveAttackTag;

	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UNiagaraComponent>> ActiveLoopingCues;

	/** Stop policy captured at spawn time, so a style/weapon swap cannot change cleanup behavior for an already-running cue. */
	UPROPERTY(Transient)
	FGameplayTagContainer ImmediateDestroyCueTags;

	/** Each semantic cue starts once per attack, preventing local prediction and server multicast from double-spawning it. */
	UPROPERTY(Transient)
	FGameplayTagContainer StartedCueTags;

	UPROPERTY(ReplicatedUsing = OnRep_PresentationState)
	FProject_JReplicatedCombatPresentationState ReplicatedPresentationState;

	int32 NextPresentationEventOrder = 0;
	int32 LastAppliedPresentationEventOrder = 0;
	int32 LastAppliedPresentationRevision = 0;
	uint32 ActiveAttackInstance = 0;
	int32 LastAppliedRecoveryEventOrder = 0;
};
