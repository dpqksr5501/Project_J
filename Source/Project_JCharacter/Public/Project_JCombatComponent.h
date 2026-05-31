// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

protected:
	// Callback when a GAS Ability is activated
	virtual void OnAbilityActivatedCallback(UGameplayAbility* Ability);

	bool TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const;
	void SetOwnedCombatStateTag(const FGameplayTag& StateTag, bool bEnabled);
	void ClearOwnedCombatStateTags();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|GAS", meta = (ToolTip = "Optional ability tag to activate when Attack is called. Leave empty while combat abilities are not authored yet."))
	FGameplayTag PrimaryAttackAbilityTag;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> OwnerASC = nullptr;

private:
	FDelegateHandle AbilityActivatedDelegateHandle;
	TSet<FGameplayTag> OwnedLooseCombatStateTags;
};
