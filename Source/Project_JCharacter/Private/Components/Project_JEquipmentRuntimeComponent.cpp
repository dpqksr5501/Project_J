#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "GameFramework/Pawn.h"
#include "Movement/Project_JMovementQueries.h"
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Equipment/Project_JGameplayEffect_EquipmentStats.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
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
	if (InEquipmentManager && (!IsValid(InEquipmentManager) || !InEquipmentManager->IsAcceptingRequests())) { return; }
	if ((bIsEndingPlay && InEquipmentManager) ||
		(BoundEquipmentManager == InEquipmentManager && (InEquipmentManager || RuntimeItems.IsEmpty())))
	{
		return;
	}

	const uint64 ThisBindingRevision = ++BindingRevision;
	auto* PreviousManager = BoundEquipmentManager;
	BoundEquipmentManager = nullptr; // Revoke before removal/grant callbacks can rebind.
	if (PreviousManager)
	{
		PreviousManager->OnEquipmentEquipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped);
		PreviousManager->OnEquipmentUnequipped.RemoveDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped);
		PreviousManager->OnRetired.Remove(ManagerRetiredHandle);
	}
	ManagerRetiredHandle.Reset();

	// Also clean up if the old manager is absent but a slot still owns a request.
	TArray<EProject_JEquipmentSlot> Keys;
	RuntimeItems.GetKeys(Keys);
	for (const EProject_JEquipmentSlot Slot : Keys)
	{
		const FProject_JEquipmentRuntimeItem* RuntimeItem = RuntimeItems.Find(Slot);
		OnEquipmentUnequipped(Slot, RuntimeItem ? RuntimeItem->ItemDef : nullptr);
		if (ThisBindingRevision != BindingRevision) { return; }
	}

	if (ThisBindingRevision != BindingRevision || (bIsEndingPlay && InEquipmentManager)) { return; }
	BoundEquipmentManager = InEquipmentManager;

	if (BoundEquipmentManager)
	{
		BoundEquipmentManager->OnEquipmentEquipped.AddDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped);
		BoundEquipmentManager->OnEquipmentUnequipped.AddDynamic(this, &UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped);
		ManagerRetiredHandle = BoundEquipmentManager->OnRetired.AddUObject(this,
			&UProject_JEquipmentRuntimeComponent::OnEquipmentManagerRetired, InEquipmentManager);

		// Initialize currently equipped items
		TArray<UProject_JEquipmentItemDefinition*> CurrentItems = BoundEquipmentManager->GetAllEquippedItems();
		for (UProject_JEquipmentItemDefinition* ItemDef : CurrentItems)
		{
			OnEquipmentEquipped(ItemDef ? ItemDef->EquipmentSlot : EProject_JEquipmentSlot::None, ItemDef);
			if (ThisBindingRevision != BindingRevision || bIsEndingPlay) { return; }
		}
	}
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentManagerRetired(UProject_JEquipmentManagerComponent* ExpectedManager)
{
	if (BoundEquipmentManager == ExpectedManager) { BindToEquipmentManager(nullptr); }
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentEquipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	if (bIsEndingPlay || IsBeingDestroyed() || !IsValid(ItemDef)) return;
	const uint64 RequestedBindingRevision = BindingRevision;

	if (RuntimeItems.Contains(Slot))
	{
		OnEquipmentUnequipped(Slot, RuntimeItems[Slot].ItemDef);
	}

	APawn* OwnerCharacter = Cast<APawn>(GetOwner());
	if (bIsEndingPlay || IsBeingDestroyed() || RequestedBindingRevision != BindingRevision ||
		!IsValid(OwnerCharacter) || OwnerCharacter->IsActorBeingDestroyed()) return;

	FProject_JEquipmentRuntimeItem NewRuntimeItem;
	NewRuntimeItem.ItemDef = ItemDef;
	NewRuntimeItem.VisualRevision = ++NextVisualRevision;

	ApplyEquipmentGameplay(*OwnerCharacter, *ItemDef, NewRuntimeItem);
	// GAS grants/stat changes may synchronously retire or rebind this component.
	// This local transaction has not been published, so teardown cannot own it yet.
	if (bIsEndingPlay || IsBeingDestroyed() || RequestedBindingRevision != BindingRevision ||
		OwnerCharacter->IsActorBeingDestroyed() || RuntimeItems.Contains(Slot))
	{
		RemoveEquipmentGameplay(NewRuntimeItem);
		return;
	}

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
	RemoveEquipmentGameplay(RuntimeItem);

	DestroyEquipmentVisual(RuntimeItem);

	ScheduleVisualRetry();
	RefreshCurrentWeaponConfiguration();
}

void UProject_JEquipmentRuntimeComponent::ApplyEquipmentGameplay(APawn& OwnerCharacter, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	if (!OwnerCharacter.HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(&OwnerCharacter);
	RuntimeItem.GrantedAbilitySystem = ASC;
	if (ASC && ItemDef.AbilitySet)
	{
		const FName GrantSource(*FString::Printf(TEXT("Equipment.%d.%s"), static_cast<int32>(ItemDef.EquipmentSlot), *ItemDef.GetPathName()));
		ItemDef.AbilitySet->GiveToAbilitySystem(ASC, &RuntimeItem.GrantedHandles, const_cast<UProject_JEquipmentItemDefinition*>(&ItemDef), GrantSource);
	}
	if (ASC && ItemDef.CombatStyleDefinition)
	{
		for (const UProject_JAbilitySet* StyleAbilitySet : ItemDef.CombatStyleDefinition->GetRuntimeAbilitySets())
		{
			if (StyleAbilitySet)
			{
				const FName GrantSource(*FString::Printf(TEXT("CombatStyle.%s.%s"), *ItemDef.CombatStyleDefinition->GetPathName(), *StyleAbilitySet->GetPathName()));
				StyleAbilitySet->GiveToAbilitySystem(ASC, &RuntimeItem.GrantedHandles, ASC->GetOwnerActor(), GrantSource);
			}
		}
	}

	if (ASC)
	{
        // Two retained avatars can represent the same persistent slot. Reuse
        // the ASC's existing provider lease: gameplay effects belong to that
        // manager/slot/definition, while each avatar still owns its visuals.
        auto* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(ASC);
        bool bNeedsEffects = true;
        uint64 EffectGeneration = 0;
        const FName EffectSource(*FString::Printf(TEXT("EquipmentEffects.%s.%d.%s"),
            *GetPathNameSafe(BoundEquipmentManager), int32(ItemDef.EquipmentSlot), *ItemDef.GetPathName()));
        if (ProjectJASC)
        {
            if (!ProjectJASC->AcquireAbilityGrantSource(EffectSource, bNeedsEffects, EffectGeneration)) { return; }
            RuntimeItem.GrantedHandles.GrantSourceGenerations.Add(EffectSource, EffectGeneration);
        }
        if (bNeedsEffects)
        {
            ApplyEquipmentEffects(*ASC, ItemDef, RuntimeItem);
            if (ProjectJASC)
            {
                // Detach local handles before callbacks. A retired generation
                // cannot register effects into a newer provider's lease.
                const auto Effects = MoveTemp(RuntimeItem.GrantedEffectHandles);
                for (const auto Handle : Effects)
                {
                    if (!ProjectJASC->RegisterGrantedEffect(EffectSource, EffectGeneration, Handle))
                    { ASC->RemoveActiveGameplayEffect(Handle); }
                }
            }
        }
		TArray<FText> Conflicts;
		FProject_JCombatConfiguration::FindInputGrantConflicts(*ASC, Conflicts);
		for (const FText& Conflict : Conflicts) { UE_LOG(LogProjectJEquipmentRuntime, Verbose, TEXT("%s"), *Conflict.ToString()); }
	}
}

void UProject_JEquipmentRuntimeComponent::RemoveEquipmentGameplay(FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	UAbilitySystemComponent* ASC = RuntimeItem.GrantedAbilitySystem.Get();
	if (ASC && ASC->IsOwnerActorAuthoritative())
	{
		GetDefault<UProject_JAbilitySet>()->TakeFromAbilitySystem(ASC, &RuntimeItem.GrantedHandles);
		RemoveEquipmentEffects(*ASC, RuntimeItem);
	}
	RuntimeItem.GrantedHandles = FProject_JAbilitySet_GrantedHandles();
	RuntimeItem.GrantedEffectHandles.Reset();
	RuntimeItem.GrantedAbilitySystem.Reset();
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

	APawn* OwnerCharacter = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerCharacter) || OwnerCharacter->IsActorBeingDestroyed() ||
		OwnerCharacter->GetNetMode() == NM_DedicatedServer) return;

	USkeletalMeshComponent* PoseSource = nullptr;
	FName AttachmentSocket = ItemDef->AttachSocketName;
	if (AttachmentSocket.IsNone())
	{
		PoseSource = Project_J::Animation::ResolveEquipmentPoseSource(*OwnerCharacter, *LoadedMesh,
			ItemDef->PoseMode == EProject_JEquipmentPoseMode::CopyPose);
	}
	else
	{
		PoseSource = Project_J::Animation::ResolveWeaponAttachmentMesh(*OwnerCharacter,
			AttachmentSocket, NAME_None, true, AttachmentSocket);
	}
	if (!PoseSource)
	{
		UE_LOG(LogProjectJEquipmentRuntime, Warning,
			TEXT("Equipment visual has no compatible pose source or attachment socket. Owner=%s Mesh=%s Slot=%d"),
			*GetNameSafe(OwnerCharacter), *GetNameSafe(LoadedMesh), int32(Slot));
		CancelEquipmentMeshLoad(RuntimeItem);
		ScheduleVisualRetry();
		return;
	}

	UProject_JModularMeshComponent* NewMeshComp = NewObject<UProject_JModularMeshComponent>(OwnerCharacter);
	NewMeshComp->SetSkeletalMesh(LoadedMesh);
	NewMeshComp->SetCastShadow(ItemDef->bCastDynamicShadow);
	NewMeshComp->SetCullDistance(ItemDef->MaxDrawDistance);

	if (ItemDef->AttachSocketName.IsNone())
	{
		if(ItemDef->PoseMode == EProject_JEquipmentPoseMode::CopyPose)
		{
			if(!NewMeshComp->AttachAndCopyPose(PoseSource))
			{
				NewMeshComp->DestroyComponent();
				CancelEquipmentMeshLoad(RuntimeItem);
				UE_LOG(LogProjectJEquipmentRuntime,Warning,TEXT("Copy-pose equipment has an incompatible base hierarchy. Item=%s"),*GetNameSafe(ItemDef));
				return;
			}
		}
		else NewMeshComp->AttachAndSetLeader(PoseSource);
	}
	else
	{
		NewMeshComp->AttachToComponent(PoseSource, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachmentSocket);
		NewMeshComp->SetLeaderPoseComponent(nullptr); 
	}

	// Install the mesh and pose source before registration can initialize cloth
	// or post-process dynamics. Do not reset the already-running body simulation.
	NewMeshComp->RegisterComponent();
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
		ApplyEquipmentStatModifiers(ASC, ItemDef, RuntimeItem);
		return;
	}

	if (!ItemDef.EquipmentEffects.IsEmpty())
	{
		for (const TSubclassOf<UGameplayEffect>& EffectClass : ItemDef.EquipmentEffects)
		{
			// Equipment buffs must have a removable lifetime. Instant base writes
			// cannot be undone on unequip and must not trigger a second stat fallback.
			if (!EffectClass || EffectClass->GetDefaultObject<UGameplayEffect>()->DurationPolicy == EGameplayEffectDurationType::Instant)
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
		ApplyEquipmentStatModifiers(ASC, ItemDef, RuntimeItem);
	}
}

void UProject_JEquipmentRuntimeComponent::RemoveEquipmentEffects(UAbilitySystemComponent& ASC, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	for (const FActiveGameplayEffectHandle& EffectHandle : RuntimeItem.GrantedEffectHandles)
	{
		if (EffectHandle.IsValid())
		{
			ASC.RemoveActiveGameplayEffect(EffectHandle);
		}
	}

	RuntimeItem.GrantedEffectHandles.Reset();
}

void UProject_JEquipmentRuntimeComponent::ApplyEquipmentStatModifiers(UAbilitySystemComponent& ASC,
	const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	const auto Handle = UProject_JGameplayEffect_EquipmentStats::ApplyBonuses(ASC, ItemDef.StatModifiers, &ItemDef);
	if (Handle.IsValid())
	{
		RuntimeItem.GrantedEffectHandles.Add(Handle);
	}
}
