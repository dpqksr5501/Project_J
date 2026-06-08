// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JBaseCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JDefaultAttributeSetData.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "SignificanceManager.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemOwnerInterface.h"


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
	EquipmentRuntime = CreateDefaultSubobject<UProject_JEquipmentRuntimeComponent>(TEXT("EquipmentRuntime"));
}

UAbilitySystemComponent* AProject_JBaseCharacter::GetAbilitySystemComponent() const
{
	if (AActor* OwnerActor = GetAbilitySystemOwnerActor())
	{
		if (IProject_JAbilitySystemOwnerInterface* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(OwnerActor))
		{
			if (UAbilitySystemComponent* OwnerASC = OwnerInterface->GetProjectJAbilitySystemComponent())
			{
				return OwnerASC;
			}
		}
	}
	return AbilitySystemComponent;
}

UProject_JAttributeSet* AProject_JBaseCharacter::GetAttributeSet() const
{
	if (AActor* OwnerActor = GetAbilitySystemOwnerActor())
	{
		if (IProject_JAbilitySystemOwnerInterface* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(OwnerActor))
		{
			if (UProject_JAttributeSet* OwnerAttr = OwnerInterface->GetProjectJAttributeSet())
			{
				return OwnerAttr;
			}
		}
	}
	return AttributeSet;
}

void AProject_JBaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeAbilitySystem();
	InitializeDefaultAttributes();
	BindEquipmentRuntimeToEquipmentManager();
	
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
				Character->SetActorTickInterval(Character->NearSignificanceTickInterval);
			}
			else if (Significance <= 1.0f)
			{
				Character->SetActorTickInterval(Character->MidSignificanceTickInterval);
			}
			else if (Significance <= 2.0f)
			{
				Character->SetActorTickInterval(Character->FarSignificanceTickInterval);
			}
			else
			{
				Character->SetActorTickInterval(Character->HiddenSignificanceTickInterval);
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
	BindEquipmentRuntimeToEquipmentManager();
}

void AProject_JBaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializeAbilitySystem();
	BindEquipmentRuntimeToEquipmentManager();
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
	if (const UProject_JAttributeSet* CurrentAttributeSet = GetAttributeSet())
	{
		return CurrentAttributeSet->GetHealth() <= 0.0f;
	}
	return false;
}

void AProject_JBaseCharacter::InitializeDefaultAttributes() const
{
	UProject_JAttributeSet* CurrentAttributeSet = GetAttributeSet();
	if (!HasAuthority() || !CurrentAttributeSet)
	{
		return;
	}

	const float DefaultMaxHealth = DefaultAttributeData ? DefaultAttributeData->MaxHealth : 100.0f;
	const float DefaultHealth = DefaultAttributeData ? DefaultAttributeData->Health : DefaultMaxHealth;
	const float DefaultMaxMana = DefaultAttributeData ? DefaultAttributeData->MaxMana : 100.0f;
	const float DefaultMana = DefaultAttributeData ? DefaultAttributeData->Mana : DefaultMaxMana;
	const float DefaultAttackPower = DefaultAttributeData ? DefaultAttributeData->AttackPower : 10.0f;
	const float DefaultDefense = DefaultAttributeData ? DefaultAttributeData->Defense : 0.0f;

	if (CurrentAttributeSet->GetMaxHealth() <= 0.0f)
	{
		CurrentAttributeSet->InitMaxHealth(FMath::Max(1.0f, DefaultMaxHealth));
	}

	if (CurrentAttributeSet->GetHealth() <= 0.0f)
	{
		CurrentAttributeSet->InitHealth(FMath::Clamp(DefaultHealth, 0.0f, CurrentAttributeSet->GetMaxHealth()));
	}

	if (CurrentAttributeSet->GetMaxMana() <= 0.0f)
	{
		CurrentAttributeSet->InitMaxMana(FMath::Max(1.0f, DefaultMaxMana));
	}

	if (CurrentAttributeSet->GetMana() <= 0.0f)
	{
		CurrentAttributeSet->InitMana(FMath::Clamp(DefaultMana, 0.0f, CurrentAttributeSet->GetMaxMana()));
	}

	if (CurrentAttributeSet->GetAttackPower() <= 0.0f)
	{
		CurrentAttributeSet->InitAttackPower(FMath::Max(0.0f, DefaultAttackPower));
	}

	if (CurrentAttributeSet->GetDefense() <= 0.0f)
	{
		CurrentAttributeSet->InitDefense(FMath::Max(0.0f, DefaultDefense));
	}
}

void AProject_JBaseCharacter::InitializeAbilitySystem()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	ASC->InitAbilityActorInfo(GetAbilitySystemOwnerActor(), this);

	if (HasAuthority())
	{
		UObject* AbilitySourceObject = GetAbilitySystemOwnerActor() ? Cast<UObject>(GetAbilitySystemOwnerActor()) : this;
		IProject_JAbilitySystemOwnerInterface* OwnerInterface = nullptr;
		if (AActor* OwnerActor = GetAbilitySystemOwnerActor())
		{
			OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(OwnerActor);
		}

		const bool bAlreadyGranted = OwnerInterface ? OwnerInterface->HasGrantedDefaultAbilities() : bDefaultAbilitiesGranted;
		if (!bAlreadyGranted)
		{
			for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
			{
				if (AbilityClass)
				{
					ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, AbilitySourceObject));
				}
			}

			if (OwnerInterface)
			{
				OwnerInterface->SetHasGrantedDefaultAbilities(true);
			}
			else
			{
				bDefaultAbilitiesGranted = true;
			}
		}
	}
}

void AProject_JBaseCharacter::BindEquipmentRuntimeToEquipmentManager()
{
	if (!EquipmentRuntime)
	{
		return;
	}

	UProject_JEquipmentManagerComponent* Manager = nullptr;
	if (APlayerState* OwningPlayerState = GetPlayerState())
	{
		Manager = OwningPlayerState->FindComponentByClass<UProject_JEquipmentManagerComponent>();
	}

	EquipmentRuntime->BindToEquipmentManager(Manager ? Manager : EquipmentManager);
}

AActor* AProject_JBaseCharacter::GetAbilitySystemOwnerActor() const
{
	return const_cast<AProject_JBaseCharacter*>(this);
}
