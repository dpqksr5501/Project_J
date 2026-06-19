// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Project_JCombatComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/**
 * Base component class for character combat capabilities.
 * Concrete jobs inherit from this to own weapon presentation and route combat input to GAS.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class PROJECT_JCHARACTER_API UProject_JCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UProject_JCombatComponent();

	virtual void BeginPlay() override;

public:
	/** Equip job-specific weapons */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void EquipWeapon() {}

	/** Unequip job-specific weapons */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void UnequipWeapon() {}

	/** Trigger basic attack or combo sequence */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void Attack();

	/** Bind this component to the owner's Ability System Component */
	virtual void BindToGAS(UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category = "Combat|GAS")
	UAbilitySystemComponent* GetOwnerAbilitySystemComponent() const { return OwnerASC.Get(); }

	/** Sends a Server-Side Rewind hit request to the server from a local client */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Combat|SSR")
	void ServerRequestSSRHit(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd);

	FProject_JCombatHitValidationResult ValidateServerHitRequest(const FProject_JCombatHitRequest& Request) const;

protected:
	bool TryActivateAbilityByInputTag(const FGameplayTag& InputTag) const;
	bool TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|GAS", meta = (ToolTip = "Preferred InputTag used to activate the primary attack ability."))
	FGameplayTag PrimaryAttackInputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|GAS", meta = (ToolTip = "Optional ability tag to activate when Attack is called. Leave empty while combat abilities are not authored yet."))
	FGameplayTag PrimaryAttackAbilityTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|SSR")
	FProject_JCombatHitValidationPolicy HitValidationPolicy;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Damage")
	TSubclassOf<UGameplayEffect> ConfirmedHitGameplayEffect;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> OwnerASC = nullptr;
};
