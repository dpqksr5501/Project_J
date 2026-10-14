#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JPlayerMaturityFixtures.generated.h"

UCLASS(NotBlueprintable, Transient)
class AProject_JPlayerMaturityASCFixture : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> CurrentASC;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return CurrentASC; }
};

UCLASS(NotBlueprintable, Transient)
class UProject_JPlayerMaturityMeleeFixture : public UProject_JGameplayAbility_Melee
{
	GENERATED_BODY()
public:
	void SetTestActive(bool bValue) { bIsActive = bValue; }
	void SetTestActorInfo(const FGameplayAbilityActorInfo* Info) { SetCurrentActorInfo(FGameplayAbilitySpecHandle(), Info); }
};

UCLASS(NotBlueprintable, Transient)
class AProject_JCombatFacingFixture : public AProject_JGreatswordCharacter
{
	GENERATED_BODY()
public:
	void ApplyTestRotationMode(bool bCombat) { ApplyCombatRotationMode(bCombat); }
	void UpdateTestMovementPolicy() { UpdateMaxWalkSpeed(); }
};
