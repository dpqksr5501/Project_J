#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "Project_JNPCActivationComponent.generated.h"

class UGameplayAbility;
class UAbilitySystemComponent;
class UProject_JTargetScoringComponent;
class UProject_JNPCActionComponent;

/** Opt-in combat capability; team comes from the caller's authoritative faction adapter. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JNPCActivationDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSubclassOf<UGameplayAbility> AttackAbility;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 AbilityLevel = 1;
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
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	TWeakObjectPtr<UProject_JTargetScoringComponent> OwnedScoring;
	TWeakObjectPtr<UProject_JNPCActionComponent> OwnedAction;
	TWeakObjectPtr<UAbilitySystemComponent> OwnedASC;
	FGameplayAbilitySpecHandle OwnedGrant;
	uint64 ActionToken = 0, ScoringRevision = 0;
	bool bChanging = false, bEnding = false;
	void ReleaseOwnedActivation();
};
