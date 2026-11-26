#pragma once

#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "Project_JGroundAttack.generated.h"

class UProject_JAttackDefinition;
class UProject_JGameplayWarningMarkerComponent;

USTRUCT()
struct FProject_JGroundWarningState
{
	GENERATED_BODY()

	UPROPERTY() float RadiusCm = 0.0f;
	UPROPERTY() double ResolveAt = 0.0;
};

/** A short-lived public world hazard. Its relevance and lifetime do not depend on the caster's replicated pawn. */
UCLASS(NotBlueprintable)
class PROJECT_JCHARACTER_API AProject_JGroundAttackWarning : public AActor
{
	GENERATED_BODY()
public:
	AProject_JGroundAttackWarning();
	void Publish(float RadiusCm, double ResolveAt);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION() void UpdateMarker();
	UPROPERTY(ReplicatedUsing=UpdateMarker) FProject_JGroundWarningState State;
	UPROPERTY(Transient) TObjectPtr<UProject_JGameplayWarningMarkerComponent> Marker;
	FTimerHandle MarkerTimer;
};

/** Server-authored single pulse at a fixed ground point. The warning and hit query share that point and radius. */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_GroundAttack : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UProject_JGameplayAbility_GroundAttack();
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	/** Class, advancement, style and equipment grants use their owner/item as the GAS source. */
	FString GetOwnerGrantFailure() const;
#endif
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Trigger) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack") TObjectPtr<UProject_JAttackDefinition> Attack;
	/** Optional cosmetic burst. Gameplay damage and the replicated warning remain authoritative. */
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack", meta=(Categories="GameplayCue")) FGameplayTag ImpactCue;
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack", meta=(ClampMin="0", ClampMax="2000")) float ForwardOffsetCm = 400.0f;
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack", meta=(ClampMin="50", ClampMax="1500")) float RadiusCm = 350.0f;
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack", meta=(ClampMin="0.5", ClampMax="5")) float WarningSeconds = 1.25f;
	UPROPERTY(EditDefaultsOnly, Category="Ground Attack", meta=(ClampMin="1", ClampMax="32")) int32 MaxTargets = 12;

private:
	FString GetConfigurationFailure(const UProject_JAttackDefinition* Definition) const;
	void Resolve();
	void OnDeathTagChanged(FGameplayTag Tag, int32 Count);
	void OnActorInfoChanging(AActor* NextOwner, AActor* NextAvatar);
	UFUNCTION() void OnAvatarEndPlay(AActor* Avatar, EEndPlayReason::Type EndPlayReason);
	UPROPERTY(Transient) TObjectPtr<AProject_JGroundAttackWarning> Warning;
	UPROPERTY(Transient) TObjectPtr<const UProject_JAttackDefinition> ActiveAttack;
	TWeakObjectPtr<UAbilitySystemComponent> ExecutionASC;
	TWeakObjectPtr<AActor> ExecutionAvatar;
	TWeakObjectPtr<UWorld> ExecutionWorld;
	uint64 ExecutionEpoch = 0;
	uint64 ExecutionSerial = 0;
	bool bEnding = false;
	FDelegateHandle DeathTagHandle;
	FDelegateHandle ActorInfoChangingHandle;
	FTimerHandle ResolveTimer;
	FVector HitOrigin = FVector::ZeroVector;
};
