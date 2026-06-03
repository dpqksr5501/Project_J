// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JBaseCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JDefaultAttributeSetData.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "SignificanceManager.h"
#include "Engine/World.h"

AProject_JBaseCharacter::AProject_JBaseCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	
	CharacterLevel = 1;

	// Create Ability System Component
	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Create Attribute Set
	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("AttributeSet"));

	// Create Equipment Manager
	EquipmentManager = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("EquipmentManager"));
}

UAbilitySystemComponent* AProject_JBaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AProject_JBaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeAbilitySystem();
	InitializeDefaultAttributes();
	
	if (USignificanceManager* SignificanceManager = USignificanceManager::Get(GetWorld()))
	{
		auto SignificanceFunc = [](USignificanceManager::FManagedObjectInfo* ObjectInfo, const FTransform& Viewpoint) -> float
		{
			AProject_JBaseCharacter* Character = Cast<AProject_JBaseCharacter>(ObjectInfo->GetObject());
			if (!Character) return 0.0f;

			const float DistanceSquared = FVector::DistSquared(Character->GetActorLocation(), Viewpoint.GetLocation());

			if (DistanceSquared < FMath::Square(Character->SignificanceNearDistance)) return 0.0f;
			if (DistanceSquared < FMath::Square(Character->SignificanceMidDistance)) return 1.0f;
			if (DistanceSquared < FMath::Square(Character->SignificanceFarDistance)) return 2.0f;
			return 3.0f;
		};

		auto PostSignificanceFunc = [](USignificanceManager::FManagedObjectInfo* ObjectInfo, float OldSignificance, float Significance, bool bFinal)
		{
			AProject_JBaseCharacter* Character = Cast<AProject_JBaseCharacter>(ObjectInfo->GetObject());
			if (!Character) return;

			Character->CurrentSignificance = Significance;

			if (Significance <= 0.0f)
			{
				Character->SetActorTickInterval(0.0f);
			}
			else if (Significance <= 1.0f)
			{
				Character->SetActorTickInterval(0.033f);
			}
			else if (Significance <= 2.0f)
			{
				Character->SetActorTickInterval(0.083f);
			}
			else
			{
				Character->SetActorTickInterval(0.15f);
			}
		};

		SignificanceManager->RegisterObject(this, FName("Character"), SignificanceFunc, USignificanceManager::EPostSignificanceType::Sequential, PostSignificanceFunc);
	}
}

void AProject_JBaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USignificanceManager* SignificanceManager = USignificanceManager::Get(GetWorld()))
	{
		SignificanceManager->UnregisterObject(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AProject_JBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilitySystem();
	InitializeDefaultAttributes();
}

void AProject_JBaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializeAbilitySystem();
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
	if (!HasAuthority() || !AttributeSet)
	{
		return;
	}

	const float DefaultMaxHealth = DefaultAttributeData ? DefaultAttributeData->MaxHealth : 100.0f;
	const float DefaultHealth = DefaultAttributeData ? DefaultAttributeData->Health : DefaultMaxHealth;
	const float DefaultMaxMana = DefaultAttributeData ? DefaultAttributeData->MaxMana : 100.0f;
	const float DefaultMana = DefaultAttributeData ? DefaultAttributeData->Mana : DefaultMaxMana;
	const float DefaultAttackPower = DefaultAttributeData ? DefaultAttributeData->AttackPower : 10.0f;
	const float DefaultDefense = DefaultAttributeData ? DefaultAttributeData->Defense : 0.0f;

	if (AttributeSet->GetMaxHealth() <= 0.0f)
	{
		AttributeSet->InitMaxHealth(FMath::Max(1.0f, DefaultMaxHealth));
	}

	if (AttributeSet->GetHealth() <= 0.0f)
	{
		AttributeSet->InitHealth(FMath::Clamp(DefaultHealth, 0.0f, AttributeSet->GetMaxHealth()));
	}

	if (AttributeSet->GetMaxMana() <= 0.0f)
	{
		AttributeSet->InitMaxMana(FMath::Max(1.0f, DefaultMaxMana));
	}

	if (AttributeSet->GetMana() <= 0.0f)
	{
		AttributeSet->InitMana(FMath::Clamp(DefaultMana, 0.0f, AttributeSet->GetMaxMana()));
	}

	if (AttributeSet->GetAttackPower() <= 0.0f)
	{
		AttributeSet->InitAttackPower(FMath::Max(0.0f, DefaultAttackPower));
	}

	if (AttributeSet->GetDefense() <= 0.0f)
	{
		AttributeSet->InitDefense(FMath::Max(0.0f, DefaultDefense));
	}
}

void AProject_JBaseCharacter::InitializeAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}
