// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

UProject_JCombatComponent::UProject_JCombatComponent()
{
	// Disable ticking by default as it's not needed for base combat capabilities
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatComponent::BindToGAS(UAbilitySystemComponent* ASC)
{
	if (!ASC) return;
	
	OwnerASC = ASC;
	
	// Bind to Ability activation events to push intent to Motion Matching
	OwnerASC->AbilityActivatedCallbacks.AddUObject(this, &UProject_JCombatComponent::OnAbilityActivatedCallback);
}

void UProject_JCombatComponent::OnAbilityActivatedCallback(UGameplayAbility* Ability)
{
	if (!Ability) return;

	// Determine intent based on ability tags
	// In a full implementation, you would:
	// 1. Get the current UProject_JLocomotionAnimStateComponent
	// 2. Set the Motion Matching Chooser Table variables (e.g. ActionIntent = Attack)
	// 3. This forces the client to predict the animation instantly while the server handles the GAS logic.
}
