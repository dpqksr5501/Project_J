#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Animation/AnimInstance.h"
#include "Project_JPlayerMaturityFixtures.generated.h"

UCLASS(NotBlueprintable, Transient)
class AProject_JPlayerMaturityASCFixture : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	TFunction<void(USkeletalMeshComponent*)> OnEquipmentAnimationInitialize;
	TFunction<void(UActorComponent*, bool)> OnCueComponentActivated;
	UFUNCTION()
	void HandleCueComponentActivated(UActorComponent* Component, bool bReset)
	{
		if (OnCueComponentActivated)
		{
			// Consume before invoking native code which can activate another pooled cue.
			auto Callback = MoveTemp(OnCueComponentActivated);
			Callback(Component, bReset);
		}
	}
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
class AProject_JCombatFacingFixture : public AProject_JSpellbladeCharacter
{
	GENERATED_BODY()
public:
	void ApplyTestRotationMode(bool bCombat) { ApplyCombatRotationMode(bCombat); }
	void UpdateTestMovementPolicy() { UpdateMaxWalkSpeed(); }
};

UCLASS(NotBlueprintable, Transient)
class AProject_JMassRetirementFixture : public AProject_JNPCCharacter
{
	GENERATED_BODY()
public:
	TFunction<void(bool)> OnHiddenChanged;
	virtual void SetActorHiddenInGame(bool bNewHidden) override
	{
		Super::SetActorHiddenInGame(bNewHidden);
		if (OnHiddenChanged)
		{
			auto Callback = MoveTemp(OnHiddenChanged);
			Callback(bNewHidden);
		}
	}
};

/** Real post-process initialization seam. Its owner consumes the callback before any nested registration. */
UCLASS(NotBlueprintable, Transient)
class UProject_JEquipmentRegistrationAnimFixture : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeInitializeAnimation() override
 {
  Super::NativeInitializeAnimation();
  if(auto* Owner=Cast<AProject_JPlayerMaturityASCFixture>(GetOwningActor());Owner && Owner->OnEquipmentAnimationInitialize)
  {
   auto Callback=MoveTemp(Owner->OnEquipmentAnimationInitialize);
   Callback(GetSkelMeshComponent());
  }
 }
};
