#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility_Melee.generated.h"

class UAnimMontage;
class UProject_JComboDefinition;
class UProject_JAttackDefinition;
struct FProject_JComboNode;

/**
 * A flexible, data-driven combo ability for MMORPG combat.
 * Listens for Gameplay Events triggered by AnimNotifies to branch combos based on input tags.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_Melee : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UProject_JGameplayAbility_Melee();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	/** Called when an input event is received during the combo window */
	UFUNCTION()
	void OnComboInputReceived(FGameplayEventData Payload);

	/** Called when the ComboWindow notify begins */
	UFUNCTION()
	void OnComboWindowOpened(FGameplayEventData Payload);

	/** Called when the Montage completely finishes or is interrupted */
	UFUNCTION()
	void OnMontageCompleted();
	
	UFUNCTION()
	void OnMontageInterrupted();

	/** Called when MeleeHit notify registers a hit */
	UFUNCTION()
	void OnMeleeHitReceived(FGameplayEventData Payload);

	/** Rotates the character towards the camera look direction or input direction for 3rd-person actions */
	void ApplyCameraDirectionRotation();
	void StartComboNode(const FProject_JComboNode& Node);
	bool TryQueueOrConsumeComboInput(FGameplayTag InputTag);
	const FProject_JComboNode* GetCurrentComboNode() const;
	FGameplayTagContainer GetOwnerGameplayTags() const;
	void BindComboInputEvents();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Tags")
	FGameplayTag MeleeHitEventTag;

private:
	FGameplayTag CurrentComboNodeTag;
	FGameplayTag QueuedInputTag;
	TObjectPtr<UProject_JComboDefinition> ActiveComboDefinition = nullptr;
	TObjectPtr<UAnimMontage> ActiveComboMontage = nullptr;
	TObjectPtr<UProject_JAttackDefinition> ActiveAttackDefinition = nullptr;
	bool bIsComboWindowOpen = false;
	bool bHasNextComboQueued = false;
	
	UPROPERTY()
	class UAbilityTask_PlayMontageAndWait* MontageTask = nullptr;

	UPROPERTY()
	TArray<class UAbilityTask_WaitGameplayEvent*> ComboEventTasks;

	/** Set of actors already hit during the current combo swing to prevent multi-hits from the same attack section */
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<const AActor>> HitActorsThisSwing;
};
