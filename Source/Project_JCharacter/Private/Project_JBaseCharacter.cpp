// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JBaseCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JDefaultAttributeSetData.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "AbilitySystem/Project_JAbilitySet.h"
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

	// ASC, AttributeSet, and EquipmentManager are NOT created as default subobjects on the base.
	// Player characters pull these from PlayerState. NPCs will construct them locally.
	AbilitySystemComponent = nullptr;
	AttributeSet = nullptr;
	EquipmentManager = nullptr;

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
	BindEquipmentRuntimeToResolvedEquipmentManager();
	
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
	BindEquipmentRuntimeToResolvedEquipmentManager();
}

void AProject_JBaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializeAbilitySystem();
	BindEquipmentRuntimeToResolvedEquipmentManager();
}



int32 AProject_JBaseCharacter::GetCharacterLevel_Implementation() const
{
	const int32 ClassStartingLevel = CharacterClassDefinition ? CharacterClassDefinition->StartingLevel : 1;
	return FMath::Max(CharacterLevel, ClassStartingLevel);
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

bool AProject_JBaseCharacter::InitializeCharacterClassDefinition(UProject_JCharacterClassDefinition* NewClassDefinition)
{
	if (!HasAuthority() || !NewClassDefinition)
	{
		return false;
	}

	if (CharacterClassDefinition == NewClassDefinition)
	{
		return true;
	}

	if (CharacterClassDefinition || AdvancementDefinition)
	{
		return false;
	}

	if (AActor* OwnerActor = GetAbilitySystemOwnerActor())
	{
		if (const IProject_JAbilitySystemOwnerInterface* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(OwnerActor);
			OwnerInterface && OwnerInterface->HasGrantedDefaultAbilities())
		{
			return false;
		}
	}
	else if (bDefaultAbilitiesGranted)
	{
		return false;
	}

	CharacterClassDefinition = NewClassDefinition;
	CharacterLevel = FMath::Max(CharacterLevel, NewClassDefinition->StartingLevel);
	InitializeDefaultAttributes(true);
	InitializeAbilitySystem();
	return true;
}

FName AProject_JBaseCharacter::GetCharacterClassId() const
{
	return CharacterClassDefinition ? CharacterClassDefinition->ClassId : NAME_None;
}

FName AProject_JBaseCharacter::GetAdvancementId() const
{
	return AdvancementDefinition ? AdvancementDefinition->AdvancementId : NAME_None;
}

bool AProject_JBaseCharacter::CanApplyAdvancementDefinition(const UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition) const
{
	if (!HasAuthority() || !NewAdvancementDefinition || NewAdvancementDefinition == AdvancementDefinition)
	{
		return false;
	}

	if (NewAdvancementDefinition->BaseClass && CharacterClassDefinition && NewAdvancementDefinition->BaseClass != CharacterClassDefinition)
	{
		return false;
	}

	if (GetCharacterLevel_Implementation() < NewAdvancementDefinition->RequiredLevel)
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	FGameplayTagContainer OwnedTags;
	ASC->GetOwnedGameplayTags(OwnedTags);
	if (!OwnedTags.HasAll(NewAdvancementDefinition->RequiredTags))
	{
		return false;
	}

	if (OwnedTags.HasAny(NewAdvancementDefinition->BlockedTags))
	{
		return false;
	}

	return true;
}

bool AProject_JBaseCharacter::ApplyAdvancementDefinition(UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition)
{
	if (!CanApplyAdvancementDefinition(NewAdvancementDefinition))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	if (NewAdvancementDefinition->AbilityGrantPolicy == EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement)
	{
		RemoveAdvancementAbilitySets(*ASC);
	}

	AdvancementDefinition = NewAdvancementDefinition;
	if (!CharacterClassDefinition && AdvancementDefinition->BaseClass)
	{
		CharacterClassDefinition = AdvancementDefinition->BaseClass;
	}

	UObject* AbilitySourceObject = GetAbilitySystemOwnerActor() ? Cast<UObject>(GetAbilitySystemOwnerActor()) : this;
	GiveAdvancementAbilitySets(*ASC, AbilitySourceObject, AdvancementGrantedHandles);
	return true;
}

void AProject_JBaseCharacter::InitializeDefaultAttributes(bool bForceReset) const
{
	UProject_JAttributeSet* CurrentAttributeSet = GetAttributeSet();
	if (!HasAuthority() || !CurrentAttributeSet)
	{
		return;
	}

	const UProject_JDefaultAttributeSetData* EffectiveDefaultAttributeData = GetEffectiveDefaultAttributeData();
	const float DefaultMaxHealth = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->MaxHealth : 100.0f;
	const float DefaultHealth = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->Health : DefaultMaxHealth;
	const float DefaultMaxMana = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->MaxMana : 100.0f;
	const float DefaultMana = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->Mana : DefaultMaxMana;
	const float DefaultAttackPower = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->AttackPower : 10.0f;
	const float DefaultDefense = EffectiveDefaultAttributeData ? EffectiveDefaultAttributeData->Defense : 0.0f;

	if (bForceReset || CurrentAttributeSet->GetMaxHealth() <= 0.0f)
	{
		CurrentAttributeSet->InitMaxHealth(FMath::Max(1.0f, DefaultMaxHealth));
	}

	if (bForceReset || CurrentAttributeSet->GetHealth() <= 0.0f)
	{
		CurrentAttributeSet->InitHealth(FMath::Clamp(DefaultHealth, 0.0f, CurrentAttributeSet->GetMaxHealth()));
	}

	if (bForceReset || CurrentAttributeSet->GetMaxMana() <= 0.0f)
	{
		CurrentAttributeSet->InitMaxMana(FMath::Max(1.0f, DefaultMaxMana));
	}

	if (bForceReset || CurrentAttributeSet->GetMana() <= 0.0f)
	{
		CurrentAttributeSet->InitMana(FMath::Clamp(DefaultMana, 0.0f, CurrentAttributeSet->GetMaxMana()));
	}

	if (bForceReset || CurrentAttributeSet->GetAttackPower() <= 0.0f)
	{
		CurrentAttributeSet->InitAttackPower(FMath::Max(0.0f, DefaultAttackPower));
	}

	if (bForceReset || CurrentAttributeSet->GetDefense() <= 0.0f)
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
		const bool bHasGrantContent =
			!DefaultAbilities.IsEmpty() ||
			(CharacterClassDefinition && !CharacterClassDefinition->AbilitySets.IsEmpty()) ||
			(AdvancementDefinition && !AdvancementDefinition->AdditionalAbilitySets.IsEmpty());
		if (!bAlreadyGranted && bHasGrantContent)
		{
			for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
			{
				if (AbilityClass)
				{
					ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, AbilitySourceObject));
				}
			}
			GiveDefaultAbilitySets(*ASC, AbilitySourceObject);

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

const UProject_JDefaultAttributeSetData* AProject_JBaseCharacter::GetEffectiveDefaultAttributeData() const
{
	if (AdvancementDefinition && AdvancementDefinition->OverrideAttributeData)
	{
		return AdvancementDefinition->OverrideAttributeData;
	}

	if (CharacterClassDefinition && CharacterClassDefinition->DefaultAttributeData)
	{
		return CharacterClassDefinition->DefaultAttributeData;
	}

	return DefaultAttributeData;
}

void AProject_JBaseCharacter::GiveDefaultAbilitySets(UAbilitySystemComponent& ASC, UObject* AbilitySourceObject)
{
	FProject_JAbilitySet_GrantedHandles IgnoredGrantedHandles;

	if (CharacterClassDefinition)
	{
		for (const UProject_JAbilitySet* AbilitySet : CharacterClassDefinition->AbilitySets)
		{
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(&ASC, &IgnoredGrantedHandles, AbilitySourceObject);
			}
		}
	}

	if (AdvancementDefinition)
	{
		if (AdvancementDefinition->BaseClass && AdvancementDefinition->BaseClass != CharacterClassDefinition)
		{
			for (const UProject_JAbilitySet* AbilitySet : AdvancementDefinition->BaseClass->AbilitySets)
			{
				if (AbilitySet)
				{
					AbilitySet->GiveToAbilitySystem(&ASC, &IgnoredGrantedHandles, AbilitySourceObject);
				}
			}
		}

		for (const UProject_JAbilitySet* AbilitySet : AdvancementDefinition->AdditionalAbilitySets)
		{
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(&ASC, &AdvancementGrantedHandles, AbilitySourceObject);
			}
		}
	}
}

void AProject_JBaseCharacter::GiveAdvancementAbilitySets(UAbilitySystemComponent& ASC, UObject* AbilitySourceObject, FProject_JAbilitySet_GrantedHandles& OutGrantedHandles) const
{
	if (!AdvancementDefinition)
	{
		return;
	}

	for (const UProject_JAbilitySet* AbilitySet : AdvancementDefinition->AdditionalAbilitySets)
	{
		if (AbilitySet)
		{
			AbilitySet->GiveToAbilitySystem(&ASC, &OutGrantedHandles, AbilitySourceObject);
		}
	}
}

void AProject_JBaseCharacter::RemoveAdvancementAbilitySets(UAbilitySystemComponent& ASC)
{
	for (const FGameplayAbilitySpecHandle& AbilitySpecHandle : AdvancementGrantedHandles.AbilitySpecHandles)
	{
		if (AbilitySpecHandle.IsValid())
		{
			ASC.ClearAbility(AbilitySpecHandle);
		}
	}

	for (const FActiveGameplayEffectHandle& EffectHandle : AdvancementGrantedHandles.GameplayEffectHandles)
	{
		if (EffectHandle.IsValid())
		{
			ASC.RemoveActiveGameplayEffect(EffectHandle);
		}
	}

	AdvancementGrantedHandles.AbilitySpecHandles.Reset();
	AdvancementGrantedHandles.GameplayEffectHandles.Reset();
}

UProject_JEquipmentManagerComponent* AProject_JBaseCharacter::ResolveEquipmentManagerForRuntime() const
{
	if (APlayerState* OwningPlayerState = GetPlayerState())
	{
		if (UProject_JEquipmentManagerComponent* PlayerStateEquipmentManager = OwningPlayerState->FindComponentByClass<UProject_JEquipmentManagerComponent>())
		{
			return PlayerStateEquipmentManager;
		}
	}

	return EquipmentManager;
}

void AProject_JBaseCharacter::BindEquipmentRuntimeToResolvedEquipmentManager()
{
	if (!EquipmentRuntime)
	{
		return;
	}

	EquipmentRuntime->BindToEquipmentManager(ResolveEquipmentManagerForRuntime());
}

AActor* AProject_JBaseCharacter::GetAbilitySystemOwnerActor() const
{
	return const_cast<AProject_JBaseCharacter*>(this);
}
