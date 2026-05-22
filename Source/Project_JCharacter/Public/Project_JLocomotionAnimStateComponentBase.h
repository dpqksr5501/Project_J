// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.generated.h"

class AProject_JPlayerCharacter;
class UAbilitySystemComponent;
class UCharacterMovementComponent;
class UCapsuleComponent;
class USkeletalMeshComponent;

/**
 * Base owner/reference cache for locomotion animation state components.
 *
 * Gameplay state transitions stay in concrete components; this base only owns
 * reusable character/component lookup so future locomotion variants can share it.
 */
UCLASS(Abstract, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JLocomotionAnimStateComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JLocomotionAnimStateComponentBase();

	virtual void BeginPlay() override;

protected:
	void CacheOwnerReferences();
	AProject_JPlayerCharacter* GetPlayerOwner() const;
	UCharacterMovementComponent* GetCachedMovementComponent() const;
	UAbilitySystemComponent* GetCachedAbilitySystemComponent() const;

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> CachedPlayerOwner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CachedMovementComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> CachedCapsuleComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> CachedAbilitySystemComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMeshComponent = nullptr;
};
