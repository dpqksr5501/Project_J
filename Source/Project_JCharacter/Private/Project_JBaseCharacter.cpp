// Fill out your copyright notice in the Description page of Project Settings.


#include "Project_JBaseCharacter.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JDefaultAttributeSetData.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "SignificanceManager.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"


AProject_JBaseCharacter::AProject_JBaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UProject_JBudgetedSkeletalMeshComponent>(ACharacter::MeshComponentName))
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

			// Significance is a measurement, not a safe global actor-tick throttle.
			// Player characters use their tick for input/locomotion and NPCs disable it
			// entirely. Individual consumers (AI, mesh URO, Mass LOD) must opt in to
			// their own budget policy rather than silently slowing every character.
		};

		SignificanceManager->RegisterObject(this, FName("Character"), SignificanceFunc, USignificanceManager::EPostSignificanceType::Sequential, PostSignificanceFunc);
	}
}

void AProject_JBaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundProgression.IsValid()) BoundProgression->OnChanged.RemoveAll(this);
	BoundProgression.Reset();
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

void AProject_JBaseCharacter::UnPossessed()
{
	if (BoundProgression.IsValid()) BoundProgression->OnChanged.RemoveAll(this);
	BoundProgression.Reset();
	Super::UnPossessed();
}

void AProject_JBaseCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializeAbilitySystem();
	BindEquipmentRuntimeToResolvedEquipmentManager();
}



int32 AProject_JBaseCharacter::GetCharacterLevel_Implementation() const
{
	if (const auto* Progression = GetProgressionComponent(); Progression && Progression->GetState().Revision > 0)
		return Progression->GetState().Level;
	const auto* Class = GetCharacterClassDefinition();
	return FMath::Max(CharacterLevel, Class ? Class->StartingLevel : 1);
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

UProject_JProgressionComponent* AProject_JBaseCharacter::GetProgressionComponent() const
{
	AActor* StateOwner = GetAbilitySystemOwnerActor();
	return StateOwner ? StateOwner->FindComponentByClass<UProject_JProgressionComponent>() : nullptr;
}

const UProject_JCharacterClassDefinition* AProject_JBaseCharacter::GetCharacterClassDefinition() const
{
	if (const auto* Progression = GetProgressionComponent(); Progression && Progression->GetState().Revision > 0)
		return Progression->GetState().ClassDefinition;
	return CharacterClassDefinition;
}

const UProject_JCharacterAdvancementDefinition* AProject_JBaseCharacter::GetAdvancementDefinition() const
{
	if (const auto* Progression = GetProgressionComponent(); Progression && Progression->GetState().Revision > 0)
		return Progression->GetState().Advancement;
	return AdvancementDefinition;
}

void AProject_JBaseCharacter::OnProgressionChanged() {}

bool AProject_JBaseCharacter::InitializeCharacterClassDefinition(UProject_JCharacterClassDefinition* NewClassDefinition)
{
	if (!HasAuthority() || !NewClassDefinition) return false;
	InitializeAbilitySystem();
	auto* Progression = GetProgressionComponent();
	const bool bHadClass = GetCharacterClassDefinition() != nullptr;
	if (!Progression || !Progression->InitializeClass(NewClassDefinition)) return false;
	if (!bHadClass) InitializeDefaultAttributes(true);
	return true;
}

FName AProject_JBaseCharacter::GetCharacterClassId() const
{
	const auto* Class = GetCharacterClassDefinition();
	return Class ? Class->ClassId : NAME_None;
}

FName AProject_JBaseCharacter::GetAdvancementId() const
{
	const auto* Advancement = GetAdvancementDefinition();
	return Advancement ? Advancement->AdvancementId : NAME_None;
}

const UProject_JCombatStyleDefinition* AProject_JBaseCharacter::GetClassCombatStyleDefinition() const
{
	const auto* Advancement = GetAdvancementDefinition();
	if (Advancement && Advancement->CombatStyleOverride) return Advancement->CombatStyleOverride;
	const auto* Class = GetCharacterClassDefinition();
	return Class ? Class->DefaultCombatStyle.Get() : nullptr;
}

bool AProject_JBaseCharacter::CanApplyAdvancementDefinition(const UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition) const
{
	const auto* Progression = GetProgressionComponent();
	return HasAuthority() && Progression && Progression->CanApplyAdvancement(NewAdvancementDefinition);
}

bool AProject_JBaseCharacter::ApplyAdvancementDefinition(UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition)
{
	auto* Progression = GetProgressionComponent();
	return HasAuthority() && Progression && Progression->ApplyAdvancement(NewAdvancementDefinition);
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
	if (!ASC)
	{
		// PlayerState can replicate away before the old avatar is destroyed.
		if (BoundProgression.IsValid()) BoundProgression->OnChanged.RemoveAll(this);
		BoundProgression.Reset();
		return;
	}

	ASC->InitAbilityActorInfo(GetAbilitySystemOwnerActor(), this);
	auto* Progression = GetProgressionComponent();
	if (!Progression && HasAuthority())
	{
		// Compatibility adapter for custom character subclasses with a local ASC.
		Progression = NewObject<UProject_JProgressionComponent>(GetAbilitySystemOwnerActor());
		GetAbilitySystemOwnerActor()->AddInstanceComponent(Progression);
		Progression->RegisterComponent();
	}
	if (BoundProgression.Get() != Progression)
	{
		if (BoundProgression.IsValid()) BoundProgression->OnChanged.RemoveAll(this);
		BoundProgression = Progression;
		if (Progression) Progression->OnChanged.AddUObject(this, &ThisClass::OnProgressionChanged);
	}
	if (Progression) Progression->InitializeDefaults(CharacterClassDefinition, AdvancementDefinition, CharacterLevel);
	OnProgressionChanged();

	if (HasAuthority())
	{
		UObject* AbilitySourceObject = GetAbilitySystemOwnerActor() ? Cast<UObject>(GetAbilitySystemOwnerActor()) : this;
		IProject_JAbilitySystemOwnerInterface* OwnerInterface = nullptr;
		if (AActor* OwnerActor = GetAbilitySystemOwnerActor())
		{
			OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(OwnerActor);
		}

		const bool bAlreadyGranted = OwnerInterface ? OwnerInterface->HasGrantedDefaultAbilities() : bDefaultAbilitiesGranted;
		const bool bHasGrantContent = !DefaultAbilities.IsEmpty();
		if (!bAlreadyGranted && bHasGrantContent)
		{
			for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
			{
				if (AbilityClass)
				{
					const FName GrantSource(*FString::Printf(TEXT("Default.%s"), *AbilityClass->GetName()));
					if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
					{
						if (!ProjectJASC->ReserveAbilityGrantSource(GrantSource))
						{
							continue;
						}
						const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, AbilitySourceObject));
						ProjectJASC->RegisterGrantedAbility(GrantSource, Handle);
					}
					else
					{
						ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, AbilitySourceObject));
					}
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

const UProject_JDefaultAttributeSetData* AProject_JBaseCharacter::GetEffectiveDefaultAttributeData() const
{
	const auto* Advancement = GetAdvancementDefinition();
	if (Advancement && Advancement->OverrideAttributeData) return Advancement->OverrideAttributeData;
	const auto* Class = GetCharacterClassDefinition();
	if (Class && Class->DefaultAttributeData) return Class->DefaultAttributeData;
	return DefaultAttributeData;
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

void AProject_JBaseCharacter::SetCharacterLevel(int32 NewLevel)
{
	if (!HasAuthority()) return;
	const int32 PreviousLevel = GetCharacterLevel_Implementation();
	int32 GuardedLevel = FMath::Max(1, NewLevel);
	if (auto* Progression = GetProgressionComponent(); Progression && Progression->GetState().Revision > 0)
	{
		if (!Progression->SetLevel(NewLevel)) return;
		GuardedLevel = Progression->GetState().Level;
	}
	CharacterLevel = GuardedLevel;
	if (PreviousLevel != GuardedLevel) InitializeDefaultAttributes(true);
}
