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

	/** Submits any discrete combat intent to GAS. The combo graph decides whether it is a valid start or branch. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	virtual void SubmitCombatInput(FGameplayTag InputTag);

	/** Compatibility entry point for old Blueprints. New code submits InputTag.Weapon.LightAttack explicitly. */
	UFUNCTION(BlueprintCallable, Category = "Combat", meta = (DeprecatedFunction, DeprecationMessage = "Use SubmitCombatInput(InputTag.Weapon.LightAttack)."))
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|SSR")
	FProject_JCombatHitValidationPolicy HitValidationPolicy;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Damage")
	TSubclassOf<UGameplayEffect> ConfirmedHitGameplayEffect;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> OwnerASC = nullptr;
};
