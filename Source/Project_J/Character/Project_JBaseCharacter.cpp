// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JBaseCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/CapsuleComponent.h"

AProject_JBaseCharacter::AProject_JBaseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	
	CharacterLevel = 1;

	// Create Ability System Component
	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Create Attribute Set
	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* AProject_JBaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AProject_JBaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

int32 AProject_JBaseCharacter::GetCharacterLevel_Implementation() const
{
	return CharacterLevel;
}

FVector AProject_JBaseCharacter::GetCombatSocketLocation_Implementation(const FName& SocketName)
{
	// Default implementation returns the location of the specified socket on the mesh
	if (GetMesh() && GetMesh()->DoesSocketExist(SocketName))
	{
		return GetMesh()->GetSocketLocation(SocketName);
	}
	// Fallback to actor location
	return GetActorLocation();
}

bool AProject_JBaseCharacter::IsDead_Implementation() const
{
	if (AttributeSet)
	{
		return AttributeSet->GetHealth() <= 0.0f;
	}
	return false;
}

void AProject_JBaseCharacter::InitializeDefaultAttributes() const
{
	// Can be implemented to apply a default GameplayEffect containing initial stats
}
