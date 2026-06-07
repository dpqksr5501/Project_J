#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility_Melee.generated.h"

class UAnimMontage;

/**
 * A flexible, data-driven combo ability for MMORPG combat.
 * Listens for Gameplay Events triggered by AnimNotifies to branch combos based on input tags.
 */
UCLASS(Abstract)
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

	/** Determines the next montage section based on the current section and the received input tag. Can be overridden in Blueprints for complex graph logic. */
	UFUNCTION(BlueprintNativeEvent, Category = "Combat|Combo")
	FName GetNextSectionForInput(FGameplayTag InputTag, FName CurrentSection) const;

	/** Rotates the character towards the camera look direction or input direction for 3rd-person actions */
	void ApplyCameraDirectionRotation();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Montage")
	UAnimMontage* AttackMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Montage")
	FName InitialSectionName = FName("Light_01");

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Tags")
	FGameplayTag MeleeHitEventTag;

private:
	FName CurrentSectionName;
	bool bIsComboWindowOpen = false;
	bool bHasNextComboQueued = false;
	FName QueuedSectionName;
	
	UPROPERTY()
	class UAbilityTask_PlayMontageAndWait* MontageTask = nullptr;

	/** Set of actors already hit during the current combo swing to prevent multi-hits from the same attack section */
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<const AActor>> HitActorsThisSwing;
};
