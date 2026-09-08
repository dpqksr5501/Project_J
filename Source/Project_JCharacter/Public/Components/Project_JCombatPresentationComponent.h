#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatPresentationComponent.generated.h"

class UNiagaraComponent;
struct FProject_JCombatVFXCueDefinition;

/** Recovery state for persistent cosmetic cues; Niagara components themselves never replicate. */
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
};

/**
 * Client-only owner for short combat cosmetics attached to the current weapon
	 * or character. It has no Niagara-component replication, pool, or per-frame
	 * tick; the server only replicates a compact recovery state for active loops.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatPresentationComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	/** Call after combat style, advancement, or visual weapon data changes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Presentation")
	void RefreshPresentation();

	UFUNCTION(BlueprintPure, Category = "Combat|Presentation")
	FGameplayTag GetActiveAttackTag() const { return ActiveAttackTag; }

private:
	const FProject_JCombatVFXCueDefinition* ResolveCue(FGameplayTag CueTag) const;
	void PlayCueLocal(FGameplayTag CueTag);
	void StopCueLocal(FGameplayTag CueTag);
	void StopAllCues();
	void PublishRecoveryState();
	void ApplyReplicatedState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayPresentationCue(FGameplayTag AttackTag, FGameplayTag CueTag, bool bStart, int32 EventOrder);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEndAttackPresentation(int32 EventOrder);

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
};
