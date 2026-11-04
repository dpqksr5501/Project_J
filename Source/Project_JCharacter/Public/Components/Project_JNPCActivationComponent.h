#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCActivationComponent.generated.h"

class UProject_JAttributeSet;
struct FProject_JConfirmedHealthGELoss;
struct FProject_JHealthGEExecutionProof;
enum class EProject_JHealthGEObservationFailure : uint8;
enum class EProject_JNPCThreatDegradation : uint8 { None, SourceLimit, ValueLimit, ObservationFailure };
struct FProject_JNPCDamageThreatStats
{
    bool bEnabled = false, bDegraded = false;
    EProject_JNPCThreatDegradation Reason = EProject_JNPCThreatDegradation::None;
    int32 Sources = 0, PeakSources = 0;
    uint64 ObservedLosses = 0, AcceptedLosses = 0, RejectedSources = 0, SourceEpochResets = 0;
    uint64 SourceLimitFailures = 0, ValueLimitFailures = 0, ObservationFailures = 0;
};
class UGameplayAbility;
class UAbilitySystemComponent;
class UProject_JTargetScoringComponent;
class UProject_JNPCActionComponent;
class UProject_JAttackDefinition;

/** Opt-in combat capability; team comes from the caller's authoritative faction adapter. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JNPCActivationDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSubclassOf<UGameplayAbility> AttackAbility;
	/** Optional authority gameplay data for NPCAttack abilities. Null retains the ability CDO's attack data. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="NPC|Attack") TObjectPtr<UProject_JAttackDefinition> AttackDefinitionOverride = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 AbilityLevel = 1;
	/** Prototype only. Count/value/observation saturation returns to distance/direction for this lease. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="NPC|Threat") bool bUseDamageThreat = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="NPC|Threat", meta=(ClampMin="1", ClampMax="2048", EditCondition="bUseDamageThreat")) int32 MaxDamageThreatSources = 128;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="NPC|Threat", meta=(ClampMin="1", ClampMax="1000000000000000", EditCondition="bUseDamageThreat")) double MaxDamageThreatPerSource = 1000000000000.0;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** One activation owns its registration, ability grant and action lease. No automatic BeginPlay activation. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JNPCActivationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="NPC|Activation")
	bool ActivateNPC(const UProject_JNPCActivationDefinition* Definition, UProject_JTargetScoringComponent* Scoring, UProject_JNPCActionComponent* Action, int32 TeamId);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="NPC|Activation") void DeactivateNPC();
	/** Includes a suspended action or a superseded action whose owned grant still needs cleanup. */
	bool HasOwnedActivation() const { return ActionToken != 0; }
	uint64 GetOwnershipToken() const { return ActionToken ? ActivationSerial : 0; }
	/** Retire only the captured activation; a later activation is a different owner. */
	bool DeactivateNPCIfOwned(uint64 Token);
	const FProject_JNPCDamageThreatStats& GetDamageThreatStats() const { return ThreatStats; }
	bool IsDamageThreatActive() const;
	bool PrepareDamageThreatSelection();
	bool ReadDamageThreat(AActor* Candidate, FProject_JNPCTargetThreat& Out) const;
	bool IsDamageThreatSourceCurrent(AActor* Candidate, FProject_JNPCTargetThreat Source) const;
	uint64 GetDamageThreatRevision() const { return DamageThreatRevision; }
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	TWeakObjectPtr<UProject_JTargetScoringComponent> OwnedScoring;
	TWeakObjectPtr<UProject_JNPCActionComponent> OwnedAction;
	TWeakObjectPtr<UAbilitySystemComponent> OwnedASC;
	FGameplayAbilitySpecHandle OwnedGrant;
	/** GAS SourceObject is weak; hold only the captured gameplay data for this owned grant. */
	UPROPERTY(Transient) TObjectPtr<UProject_JAttackDefinition> OwnedAttackDefinition;
	uint64 ActionToken = 0, ScoringRevision = 0;
	uint64 ActivationSerial = 0;
	FDelegateHandle ActorInfoChangingHandle;
	bool bChanging = false, bEnding = false;
	TWeakObjectPtr<UProject_JAttributeSet> ThreatAttributes;
	TMap<TWeakObjectPtr<AActor>, FProject_JNPCTargetThreat> DamageThreat;
	FProject_JNPCDamageThreatStats ThreatStats;
	FDelegateHandle HealthLossHandle, HealthObservationFailureHandle;
	uint64 ThreatRecipientEpoch = 0, ThreatObservationBaseline = 0, DamageThreatRevision = 0;
	int32 ThreatSourceLimit = 0;
	double ThreatValueLimit = 0;
	bool IsCurrentDamageThreatLease() const;
	void StopDamageThreatObservation();
	void DegradeDamageThreat(EProject_JNPCThreatDegradation Reason);
	void OnConfirmedHealthGELoss(const FProject_JConfirmedHealthGELoss& Loss);
	void OnHealthGEObservationFailed(const FProject_JHealthGEExecutionProof& Proof, EProject_JHealthGEObservationFailure Failure);
	void ReleaseOwnedActivation();
	void OnActorInfoChanging(AActor* NextOwner, AActor* NextAvatar);
};
