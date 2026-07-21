#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatHitValidationComponent.generated.h"

class UAbilitySystemComponent;
class UProject_JAttackDefinition;

/** Shared server-authoritative melee hit validation for all player jobs. */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatHitValidationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatHitValidationComponent();

	/** Called by the authoritative/predicted melee ability when a new combo node starts. */
	void BeginAttackNode(FGameplayTag AttackNodeTag, UProject_JAttackDefinition* AttackDefinition);
	void EndAttack();
	void SetHitWindowOpen(bool bOpen);

	/** Local predicted client path. Sequence is generated internally and verified by the server. */
	void SubmitPredictedHit(AActor* HitActor, float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd);

	/** Authority/AI path; uses the same active-node, window and duplicate validation. */
	bool ProcessAuthorityHit(AActor* HitActor);

	FGameplayTag GetActiveAttackNodeTag() const { return ActiveAttackNodeTag; }
	const UProject_JAttackDefinition* GetActiveAttackDefinition() const { return ActiveAttackDefinition.Get(); }

	UFUNCTION(Server, Unreliable)
	void ServerRequestSSRHit(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd, FGameplayTag AttackNodeTag, int32 RequestSequence);

	FProject_JCombatHitValidationResult ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const;

private:
	UAbilitySystemComponent* ResolveOwnerAbilitySystemComponent() const;
	bool ApplyConfirmedHit(AActor* HitActor);
	EProject_JCombatHitValidationFailure ValidateActiveAttack(const FProject_JCombatHitRequest& Request);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|SSR", meta = (AllowPrivateAccess = "true"))
	FProject_JCombatHitValidationPolicy HitValidationPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JAttackDefinition> ActiveAttackDefinition = nullptr;

	FGameplayTag ActiveAttackNodeTag;
	TSet<TWeakObjectPtr<const AActor>> ServerHitActors;
	bool bHitWindowOpen = false;
	int32 LocalRequestSequence = 0;
	int32 LastServerRequestSequence = 0;
	double RateWindowStartSeconds = 0.0;
	int32 RequestsInRateWindow = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|SSR", meta = (ClampMin = "1", ClampMax = "120"))
	int32 MaxRequestsPerSecond = 32;
};
