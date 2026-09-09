// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "System/Project_JAssetManager.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Optimization/Project_JRetryDelay.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Project_JPlayerCharacter.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJEquipmentRuntime, Log, All);

UProject_JEquipmentRuntimeComponent::UProject_JEquipmentRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // Only exists to drive visual/ASC on local/server
}

void UProject_JEquipmentRuntimeComponent::BeginPlay()
{
	bIsEndingPlay = false;
	Super::BeginPlay();
}

void UProject_JEquipmentRuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsEndingPlay = true;
	BindToEquipmentManager(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UProject_JEquipmentRuntimeComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// A component may be destroyed before BeginPlay (and therefore without EndPlay).
	bIsEndingPlay = true;
	BindToEquipmentManager(nullptr);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JEquipmentRuntimeComponent::BindToEquipmentManager(UProject_JEquipmentManagerComponent* InEquipmentManager)
{
	if ((bIsEndingPlay && InEquipmentManager) ||
		(BoundEquipmentManager == InEquipmentManager && (InEquipmentManager || RuntimeItems.IsEmpty())))
	{
		return;
	}

	if (BoundEquipmentManager)
	{
		BoundEquipmentManager->OnEquipmentEquipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped);
		BoundEquipmentManager->OnEquipmentUnequipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped);
	}

	// Also clean up if the old manager is absent but a slot still owns a request.
	TArray<EProject_JEquipmentSlot> Keys;
	RuntimeItems.GetKeys(Keys);
	for (const EProject_JEquipmentSlot Slot : Keys)
	{
		const FProject_JEquipmentRuntimeItem* RuntimeItem = RuntimeItems.Find(Slot);
		OnEquipmentUnequipped(Slot, RuntimeItem ? RuntimeItem->ItemDef : nullptr);
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
	if (bIsEndingPlay || !IsValid(ItemDef)) return;

	if (RuntimeItems.Contains(Slot))
	{
		OnEquipmentUnequipped(Slot, RuntimeItems[Slot].ItemDef);
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	FProject_JEquipmentRuntimeItem NewRuntimeItem;
	NewRuntimeItem.ItemDef = ItemDef;
	NewRuntimeItem.VisualRevision = ++NextVisualRevision;

	ApplyEquipmentGameplay(*OwnerCharacter, *ItemDef, NewRuntimeItem);

	RuntimeItems.Add(Slot, NewRuntimeItem);
	RefreshCurrentWeaponConfiguration();

	if (OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		StartLocalSpawnEquipment(Slot, ItemDef);
	}
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	// Revoke first: cancellation callbacks must not observe or reuse this weapon.
	// Own a copy across callbacks rather than retaining a mutable map reference.
	FProject_JEquipmentRuntimeItem RuntimeItem;
	if (!RuntimeItems.RemoveAndCopyValue(Slot, RuntimeItem)) return;
	if (Slot == EProject_JEquipmentSlot::Weapon) { WeaponRevoked.Broadcast(); }
	UProject_JEquipmentItemDefinition* RuntimeItemDef = RuntimeItem.ItemDef ? RuntimeItem.ItemDef : ItemDef;

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && RuntimeItemDef)
	{
		RemoveEquipmentGameplay(*OwnerCharacter, *RuntimeItemDef, RuntimeItem);
	}

	DestroyEquipmentVisual(RuntimeItem);

	ScheduleVisualRetry();
	RefreshCurrentWeaponConfiguration();
}

void UProject_JEquipmentRuntimeComponent::ApplyEquipmentGameplay(ACharacter& OwnerCharacter, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	if (!OwnerCharacter.HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(&OwnerCharacter);
	if (ASC && ItemDef.AbilitySet)
	{
		const FName GrantSource(*FString::Printf(TEXT("Equipment.%d.%s"), static_cast<int32>(ItemDef.EquipmentSlot), *ItemDef.GetName()));
		ItemDef.AbilitySet->GiveToAbilitySystem(ASC, &RuntimeItem.GrantedHandles, const_cast<UProject_JEquipmentItemDefinition*>(&ItemDef), GrantSource);
	}
	if (ASC && ItemDef.CombatStyleDefinition)
	{
		for (const UProject_JAbilitySet* StyleAbilitySet : ItemDef.CombatStyleDefinition->AbilitySets)
		{
			if (StyleAbilitySet)
			{
				const FName GrantSource(*FString::Printf(TEXT("CombatStyle.%s.%s"), *ItemDef.CombatStyleDefinition->CombatStyleTag.ToString(), *StyleAbilitySet->GetName()));
				StyleAbilitySet->GiveToAbilitySystem(ASC, &RuntimeItem.GrantedHandles, const_cast<UProject_JEquipmentItemDefinition*>(&ItemDef), GrantSource);
			}
		}
	}

	if (ASC)
	{
		ApplyEquipmentEffects(*ASC, ItemDef, RuntimeItem);
	}
	else
	{
		ApplyEquipmentStatModifiers(&ItemDef, 1.0f);
	}
}

void UProject_JEquipmentRuntimeComponent::RemoveEquipmentGameplay(ACharacter& OwnerCharacter, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	if (!OwnerCharacter.HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(&OwnerCharacter);
	if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC))
	{
		for (const FName GrantSourceId : RuntimeItem.GrantedHandles.GrantSourceIds)
		{
			ProjectJASC->RemoveAbilityGrantSource(GrantSourceId);
		}
		RuntimeItem.GrantedHandles.AbilitySpecHandles.Reset();
		RuntimeItem.GrantedHandles.GameplayEffectHandles.Reset();
		RuntimeItem.GrantedHandles.GrantSourceIds.Reset();
	}

	if (ASC)
	{
		RemoveEquipmentEffects(*ASC, RuntimeItem);
	}
	else
	{
		ApplyEquipmentStatModifiers(&ItemDef, -1.0f);
	}
}

void UProject_JEquipmentRuntimeComponent::StartLocalSpawnEquipment(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	check(IsInGameThread());
	if (bIsEndingPlay || !IsValid(ItemDef) || ItemDef->EquipmentMesh.IsNull()) return;
	FProject_JEquipmentRuntimeItem* RuntimeItem = RuntimeItems.Find(Slot);
	if (!RuntimeItem || RuntimeItem->ItemDef != ItemDef) return;
	if (RuntimeItem->SpawnedMesh || RuntimeItem->VisualLoadToken || RuntimeItem->VisualAttempts >= MaxVisualAttempts
		|| FPlatformTime::Seconds() < RuntimeItem->NextVisualRetry) { return; }
	RuntimeItem->NextVisualRetry = FPlatformTime::Seconds() + ProjectJ::RetryDelay(RuntimeItem->VisualAttempts++, GetUniqueID() + uint32(Slot), 0.5, 8.0);
	CancelEquipmentMeshLoad(*RuntimeItem);

	TWeakObjectPtr<UProject_JEquipmentRuntimeComponent> WeakThis(this);
	TWeakObjectPtr<UProject_JEquipmentItemDefinition> WeakItemDef(ItemDef);
	const FSoftObjectPath RequestedPath = ItemDef->EquipmentMesh.ToSoftObjectPath();
	auto* Service = GetWorld() ? GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>() : nullptr;
	if (!Service) { return; }
	VisualAssets = Service;
	const uint64 Revision = RuntimeItem->VisualRevision;
	RuntimeItem->VisualLoadToken = Service->Request(this, RequestedPath,
		[WeakThis, WeakItemDef, Slot, RequestedPath, Revision](UObject* Asset)
		{
			if (UProject_JEquipmentRuntimeComponent* StrongThis = WeakThis.Get())
			{
				UProject_JEquipmentItemDefinition* LoadedItemDef = WeakItemDef.Get();
				auto* Current = StrongThis->RuntimeItems.Find(Slot);
				if (Current && Current->VisualRevision == Revision && LoadedItemDef && LoadedItemDef->EquipmentMesh.ToSoftObjectPath() == RequestedPath)
				{
					if (Asset) { StrongThis->OnEquipmentMeshLoaded(Slot, LoadedItemDef); }
					else
					{
						StrongThis->CancelEquipmentMeshLoad(*Current);
						StrongThis->ScheduleVisualRetry();
					}
				}
			}
		}
	);
	if (!RuntimeItem->VisualLoadToken)
	{
		UE_LOG(LogProjectJEquipmentRuntime, Verbose, TEXT("Visual admission deferred. Slot=%d Item=%s Attempt=%u/%u"), int32(Slot), *GetNameSafe(ItemDef), RuntimeItem->VisualAttempts, MaxVisualAttempts);
	}
	ScheduleVisualRetry();
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentMeshLoaded(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	check(IsInGameThread());
	if (bIsEndingPlay || !IsValid(ItemDef) || !RuntimeItems.Contains(Slot)) return;

	FProject_JEquipmentRuntimeItem& RuntimeItem = RuntimeItems[Slot];
	if (RuntimeItem.ItemDef != ItemDef) return;
	// The shared visual lease pins the asset across budgeted application and until unequip.
	if (RuntimeItem.SpawnedMesh) return;
	USkeletalMesh* LoadedMesh = ItemDef->EquipmentMesh.Get();
	if (!LoadedMesh)
	{
		if (auto* Service = VisualAssets.Get()) { Service->Release(RuntimeItem.VisualLoadToken); }
		RuntimeItem.VisualLoadToken = 0;
		ScheduleVisualRetry();
		UE_LOG(LogProjectJEquipmentRuntime, Warning, TEXT("Equipment mesh load failed. Item=%s Path=%s"),
			*GetNameSafe(ItemDef), *ItemDef->EquipmentMesh.ToSoftObjectPath().ToString());
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerCharacter) || OwnerCharacter->IsActorBeingDestroyed() ||
		OwnerCharacter->GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* MainMesh = OwnerCharacter->GetMesh();
	if (!MainMesh) return;

	UProject_JModularMeshComponent* NewMeshComp = NewObject<UProject_JModularMeshComponent>(OwnerCharacter);
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetSkeletalMesh(LoadedMesh);
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

void UProject_JEquipmentRuntimeComponent::CancelEquipmentMeshLoad(FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	check(IsInGameThread());
	if (RuntimeItem.VisualLoadToken)
	{
		if (auto* Service = VisualAssets.Get()) { Service->Release(RuntimeItem.VisualLoadToken); }
		RuntimeItem.VisualLoadToken = 0;
	}
}

uint64 UProject_JEquipmentRuntimeComponent::GetWeaponRevision() const
{
	const auto* Weapon = RuntimeItems.Find(EProject_JEquipmentSlot::Weapon);
	return !bIsEndingPlay && Weapon && IsValid(Weapon->ItemDef) ? Weapon->VisualRevision : 0;
}

void UProject_JEquipmentRuntimeComponent::RetryEquipmentVisuals()
{
	check(IsInGameThread());
	if (bIsEndingPlay || !GetOwner() || GetOwner()->GetNetMode() == NM_DedicatedServer) { return; }
	for (auto& Pair : RuntimeItems)
	{
		if (!Pair.Value.SpawnedMesh && !Pair.Value.VisualLoadToken) { StartLocalSpawnEquipment(Pair.Key, Pair.Value.ItemDef); }
	}
	ScheduleVisualRetry();
}

void UProject_JEquipmentRuntimeComponent::ScheduleVisualRetry()
{
	if (!GetWorld()) { return; }
	GetWorld()->GetTimerManager().ClearTimer(VisualRetryTimer);
	if (bIsEndingPlay || IsBeingDestroyed() || GetWorld()->bIsTearingDown || !IsValid(GetOwner())
		|| GetOwner()->IsActorBeingDestroyed() || GetOwner()->GetNetMode() == NM_DedicatedServer) { return; }
	double Earliest = TNumericLimits<double>::Max();
	for (const auto& Pair : RuntimeItems)
	{
		const auto& Item = Pair.Value;
		if (!Item.SpawnedMesh && !Item.VisualLoadToken && Item.VisualAttempts < MaxVisualAttempts
			&& IsValid(Item.ItemDef) && !Item.ItemDef->EquipmentMesh.IsNull())
		{ Earliest = FMath::Min(Earliest, Item.NextVisualRetry); }
	}
	if (Earliest != TNumericLimits<double>::Max())
	{
		GetWorld()->GetTimerManager().SetTimer(VisualRetryTimer, this, &ThisClass::RetryEquipmentVisuals,
			float(FMath::Max(0.05, Earliest - FPlatformTime::Seconds())), false);
	}
}

void UProject_JEquipmentRuntimeComponent::DestroyEquipmentVisual(FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	CancelEquipmentMeshLoad(RuntimeItem);
	if (RuntimeItem.SpawnedMesh)
	{
		RuntimeItem.SpawnedMesh->DestroyComponent();
		RuntimeItem.SpawnedMesh = nullptr;
	}
}

void UProject_JEquipmentRuntimeComponent::RefreshCurrentWeaponConfiguration()
{
	AProject_JPlayerCharacter* OwnerPlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!OwnerPlayer) return;

	OwnerPlayer->SetCurrentEquipmentConfiguration(ResolveCurrentCombatStyle(), ResolveCurrentWeaponPresentationProfile());
}

UProject_JCombatStyleDefinition* UProject_JEquipmentRuntimeComponent::ResolveCurrentCombatStyle() const
{
	if (const FProject_JEquipmentRuntimeItem* WeaponItem = RuntimeItems.Find(EProject_JEquipmentSlot::Weapon))
	{
		return WeaponItem->ItemDef ? WeaponItem->ItemDef->CombatStyleDefinition.Get() : nullptr;
	}
	return nullptr;
}

UProject_JWeaponPresentationProfile* UProject_JEquipmentRuntimeComponent::ResolveCurrentWeaponPresentationProfile() const
{
	if (const FProject_JEquipmentRuntimeItem* WeaponItem = RuntimeItems.Find(EProject_JEquipmentSlot::Weapon))
	{
		return WeaponItem->ItemDef ? WeaponItem->ItemDef->WeaponPresentationProfile.Get() : nullptr;
	}
	return nullptr;
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
