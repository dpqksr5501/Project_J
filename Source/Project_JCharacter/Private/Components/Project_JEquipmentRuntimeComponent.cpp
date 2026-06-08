// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Project_JAttributeSet.h"
#include "System/Project_JAssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Project_JPlayerCharacter.h"
#include "AbilitySystem/Project_JAbilitySet.h"

UProject_JEquipmentRuntimeComponent::UProject_JEquipmentRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // Only exists to drive visual/ASC on local/server
}

void UProject_JEquipmentRuntimeComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UProject_JEquipmentRuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BindToEquipmentManager(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UProject_JEquipmentRuntimeComponent::BindToEquipmentManager(UProject_JEquipmentManagerComponent* InEquipmentManager)
{
	if (BoundEquipmentManager == InEquipmentManager)
	{
		return;
	}

	if (BoundEquipmentManager)
	{
		BoundEquipmentManager->OnEquipmentEquipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped);
		BoundEquipmentManager->OnEquipmentUnequipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped);
		
		// Cleanup existing runtime items
		TArray<EProject_JEquipmentSlot> Keys;
		RuntimeItems.GetKeys(Keys);
		for (const EProject_JEquipmentSlot Slot : Keys)
		{
			const FProject_JEquipmentRuntimeItem* RuntimeItem = RuntimeItems.Find(Slot);
			OnEquipmentUnequipped(Slot, RuntimeItem ? RuntimeItem->ItemDef : nullptr);
		}
	}

	BoundEquipmentManager = InEquipmentManager;

	if (BoundEquipmentManager)
	{
		BoundEquipmentManager->OnEquipmentEquipped.AddDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped);
		BoundEquipmentManager->OnEquipmentUnequipped.AddDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped);

		// Initialize currently equipped items
		TArray<UProject_JEquipmentItemDefinition*> CurrentItems = BoundEquipmentManager->GetAllEquippedItems();
		for (UProject_JEquipmentItemDefinition* ItemDef : CurrentItems)
		{
			OnEquipmentEquipped(ItemDef ? ItemDef->EquipmentSlot : EProject_JEquipmentSlot::None, ItemDef);
		}
	}
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef) return;

	if (RuntimeItems.Contains(Slot))
	{
		OnEquipmentUnequipped(Slot, RuntimeItems[Slot].ItemDef);
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	FProject_JEquipmentRuntimeItem NewRuntimeItem;
	NewRuntimeItem.ItemDef = ItemDef;

	// Grant GAS Ability Set (Authority Only)
	if (OwnerCharacter->HasAuthority())
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
		if (ASC && ItemDef->AbilitySet)
		{
			ItemDef->AbilitySet->GiveToAbilitySystem(ASC, &NewRuntimeItem.GrantedHandles);
		}
		if (ASC)
		{
			ApplyEquipmentEffects(*ASC, *ItemDef, NewRuntimeItem);
		}
		else
		{
			ApplyEquipmentStatModifiers(ItemDef, 1.0f);
		}
	}

	RuntimeItems.Add(Slot, NewRuntimeItem);
	RefreshCurrentWeaponAnimProfile();

	if (OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		StartLocalSpawnEquipment(Slot, ItemDef);
	}
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!RuntimeItems.Contains(Slot)) return;

	FProject_JEquipmentRuntimeItem& RuntimeItem = RuntimeItems[Slot];
	UProject_JEquipmentItemDefinition* RuntimeItemDef = RuntimeItem.ItemDef ? RuntimeItem.ItemDef : ItemDef;
	if (!RuntimeItemDef) return;

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());

	// Remove ASC Grants
	if (OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
		if (ASC && RuntimeItemDef->AbilitySet)
		{
			RuntimeItemDef->AbilitySet->TakeFromAbilitySystem(ASC, &RuntimeItem.GrantedHandles);
		}
		if (ASC)
		{
			RemoveEquipmentEffects(*ASC, RuntimeItem);
		}
		else
		{
			ApplyEquipmentStatModifiers(RuntimeItemDef, -1.0f);
		}
	}

	// Remove Visuals
	if (RuntimeItem.SpawnedMesh)
	{
		RuntimeItem.SpawnedMesh->DestroyComponent();
		RuntimeItem.SpawnedMesh = nullptr;
	}

	RuntimeItems.Remove(Slot);
	RefreshCurrentWeaponAnimProfile();
}

void UProject_JEquipmentRuntimeComponent::StartLocalSpawnEquipment(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || ItemDef->EquipmentMesh.IsNull()) return;

	TWeakObjectPtr<UProject_JEquipmentRuntimeComponent> WeakThis(this);
	UProject_JAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
		ItemDef->EquipmentMesh.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda([WeakThis, Slot, ItemDef]()
		{
			if (UProject_JEquipmentRuntimeComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->OnEquipmentMeshLoaded(Slot, ItemDef);
			}
		})
	);
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentMeshLoaded(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || !RuntimeItems.Contains(Slot)) return;

	FProject_JEquipmentRuntimeItem& RuntimeItem = RuntimeItems[Slot];
	if (RuntimeItem.ItemDef != ItemDef) return;
	if (RuntimeItem.SpawnedMesh) return;

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	USkeletalMeshComponent* MainMesh = OwnerCharacter->GetMesh();
	if (!MainMesh) return;

	UProject_JModularMeshComponent* NewMeshComp = NewObject<UProject_JModularMeshComponent>(OwnerCharacter);
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetSkeletalMesh(ItemDef->EquipmentMesh.Get());
	NewMeshComp->SetCastShadow(ItemDef->bCastDynamicShadow);
	NewMeshComp->SetCullDistance(ItemDef->MaxDrawDistance);

	if (ItemDef->AttachSocketName.IsNone())
	{
		NewMeshComp->AttachAndSetLeader(MainMesh);
	}
	else
	{
		NewMeshComp->AttachToComponent(MainMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ItemDef->AttachSocketName);
		NewMeshComp->SetLeaderPoseComponent(nullptr); 
	}

	RuntimeItem.SpawnedMesh = NewMeshComp;
}

void UProject_JEquipmentRuntimeComponent::RefreshCurrentWeaponAnimProfile()
{
	AProject_JPlayerCharacter* OwnerPlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!OwnerPlayer) return;

	UProject_JWeaponAnimProfile* SelectedWeaponAnimProfile = nullptr;
	
	// Just pick the first weapon we have equipped
	for (auto& Pair : RuntimeItems)
	{
		UProject_JEquipmentItemDefinition* ItemDef = Pair.Value.ItemDef;
		if (ItemDef && ItemDef->EquipmentSlot == EProject_JEquipmentSlot::Weapon && ItemDef->WeaponAnimProfile)
		{
			SelectedWeaponAnimProfile = ItemDef->WeaponAnimProfile;
			break;
		}
	}

	OwnerPlayer->SetCurrentWeaponAnimProfile(SelectedWeaponAnimProfile);
}

void UProject_JEquipmentRuntimeComponent::ApplyEquipmentEffects(UAbilitySystemComponent& ASC, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	if (ItemDef.StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly)
	{
		ApplyEquipmentStatModifiers(&ItemDef, 1.0f);
		RuntimeItem.bAppliedStatModifierFallback = true;
		return;
	}

	if (!ItemDef.EquipmentEffects.IsEmpty())
	{
		for (const TSubclassOf<UGameplayEffect>& EffectClass : ItemDef.EquipmentEffects)
		{
			if (!EffectClass)
			{
				continue;
			}

			FGameplayEffectContextHandle EffectContext = ASC.MakeEffectContext();
			EffectContext.AddSourceObject(&ItemDef);
			const FGameplayEffectSpecHandle SpecHandle = ASC.MakeOutgoingSpec(EffectClass, 1.0f, EffectContext);
			if (SpecHandle.IsValid())
			{
				const FActiveGameplayEffectHandle ActiveHandle = ASC.ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				if (ActiveHandle.IsValid())
				{
					RuntimeItem.GrantedEffectHandles.Add(ActiveHandle);
				}
			}
		}
	}

	if (RuntimeItem.GrantedEffectHandles.IsEmpty() &&
		ItemDef.StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::GameplayEffectsThenStatModifiers)
	{
		ApplyEquipmentStatModifiers(&ItemDef, 1.0f);
		RuntimeItem.bAppliedStatModifierFallback = true;
	}
}

void UProject_JEquipmentRuntimeComponent::RemoveEquipmentEffects(UAbilitySystemComponent& ASC, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	if (RuntimeItem.bAppliedStatModifierFallback)
	{
		ApplyEquipmentStatModifiers(RuntimeItem.ItemDef, -1.0f);
		RuntimeItem.bAppliedStatModifierFallback = false;
	}

	for (const FActiveGameplayEffectHandle& EffectHandle : RuntimeItem.GrantedEffectHandles)
	{
		if (EffectHandle.IsValid())
		{
			ASC.RemoveActiveGameplayEffect(EffectHandle);
		}
	}

	RuntimeItem.GrantedEffectHandles.Reset();
}

void UProject_JEquipmentRuntimeComponent::ApplyEquipmentStatModifiers(const UProject_JEquipmentItemDefinition* ItemDef, float Sign) const
{
	AActor* OwnerActor = GetOwner();
	UAbilitySystemComponent* ASC = OwnerActor ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor) : nullptr;
	if (!ASC || !ItemDef || !OwnerActor || !OwnerActor->HasAuthority()) return;

	for (const FProject_JEquipmentStatModifier& Modifier : ItemDef->StatModifiers)
	{
		if (FMath::IsNearlyZero(Modifier.Value)) continue;

		const float SignedValue = Modifier.Value * Sign;
		switch (Modifier.Stat)
		{
		case EProject_JEquipmentStat::MaxHealth:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetMaxHealthAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::MaxMana:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetMaxManaAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::AttackPower:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetAttackPowerAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::Defense:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetDefenseAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		}
	}

	if (const UProject_JAttributeSet* AttributeSet = Cast<UProject_JAttributeSet>(ASC->GetAttributeSet(UProject_JAttributeSet::StaticClass())))
	{
		if (AttributeSet->GetHealth() > AttributeSet->GetMaxHealth())
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, AttributeSet->GetMaxHealth());

		if (AttributeSet->GetMana() > AttributeSet->GetMaxMana())
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetManaAttribute(), EGameplayModOp::Override, AttributeSet->GetMaxMana());
	}
}
