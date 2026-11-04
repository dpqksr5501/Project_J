#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectArray.h"
#include "Movement/Project_JMovementQueries.h"
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JCopyPoseAnimInstance.h"
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
#include "Misc/App.h"
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
	SetIsReplicatedByDefault(true);
	PublicEquipment.OwnerRuntime = this;
}

void UProject_JEquipmentRuntimeComponent::BeginPlay()
{
	bIsEndingPlay = false;
	PublicEquipment.OwnerRuntime = this;
	Super::BeginPlay();
	if (UsesReplicatedPresentation()) BindToEquipmentManager(nullptr);
}

void UProject_JEquipmentRuntimeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsEndingPlay = true;
	PublicEquipment.OwnerRuntime = nullptr;
	BindToEquipmentManager(nullptr);

	Super::EndPlay(EndPlayReason);
}

void UProject_JEquipmentRuntimeComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// A component may be destroyed before BeginPlay (and therefore without EndPlay).
	bIsEndingPlay = true;
	PublicEquipment.OwnerRuntime = nullptr;
	BindToEquipmentManager(nullptr);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JEquipmentRuntimeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JEquipmentRuntimeComponent, PublicEquipment, COND_SkipOwner);
}

bool UProject_JEquipmentRuntimeComponent::UsesReplicatedPresentation() const
{
	const auto* Owner = GetOwner();
	return Owner && !Owner->HasAuthority() && !Owner->HasLocalNetOwner();
}

UProject_JEquipmentItemDefinition* UProject_JEquipmentRuntimeComponent::GetPresentedItemInSlot(EProject_JEquipmentSlot Slot) const
{
	const auto* Item = RuntimeItems.Find(Slot);
	return !bIsEndingPlay && !IsBeingDestroyed() && Item && IsValid(Item->ItemDef) ? Item->ItemDef : nullptr;
}

void UProject_JEquipmentRuntimeComponent::BindToEquipmentManager(UProject_JEquipmentManagerComponent* InEquipmentManager)
{
	const bool bRemote = !bIsEndingPlay && UsesReplicatedPresentation();
	// Simulated clients consume their own Pawn's complete public snapshot, even
	// if PlayerState has not resolved or this is a detached, retained rider.
	if (bRemote) InEquipmentManager = nullptr;
	if (InEquipmentManager && (!IsValid(InEquipmentManager) || !InEquipmentManager->IsAcceptingRequests())) return;
	if (bIsEndingPlay && InEquipmentManager) return;
	if (bUsingReplicatedPresentation == bRemote && BoundEquipmentManager == InEquipmentManager &&
		(InEquipmentManager || RuntimeItems.IsEmpty() || bRemote))
	{
		if (bRemote) ReconcilePublicEquipment();
		return;
	}

	const TStrongObjectPtr<UProject_JEquipmentRuntimeComponent> KeepSelf(this);
	const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepIncoming(InEquipmentManager);
	const uint64 ThisBindingRevision = ++BindingRevision;
	bUsingReplicatedPresentation = bRemote;
	auto* PreviousManager = BoundEquipmentManager;
	BoundEquipmentManager = nullptr;
	if (PreviousManager)
	{
		PreviousManager->OnEquipmentEquipped.RemoveDynamic(this, &ThisClass::OnEquipmentEquipped);
		PreviousManager->OnEquipmentUnequipped.RemoveDynamic(this, &ThisClass::OnEquipmentUnequipped);
		PreviousManager->OnRetired.Remove(ManagerRetiredHandle);
	}
	ManagerRetiredHandle.Reset();

	TArray<EProject_JEquipmentSlot> Keys;
	RuntimeItems.GetKeys(Keys);
	for (const EProject_JEquipmentSlot Slot : Keys)
	{
		const auto* Item = RuntimeItems.Find(Slot);
		OnEquipmentUnequipped(Slot, Item ? Item->ItemDef : nullptr);
		if (ThisBindingRevision != BindingRevision) return;
	}
	if (ThisBindingRevision != BindingRevision || (bIsEndingPlay && InEquipmentManager)) return;
	// Weapon/cancellation callbacks can retire the incoming manager before we
	// subscribe to OnRetired; a live pointer alone cannot authorize the seed.
	if (InEquipmentManager && (!IsValid(InEquipmentManager) || !InEquipmentManager->IsAcceptingRequests())) return;
	BoundEquipmentManager = InEquipmentManager;
	if (BoundEquipmentManager)
	{
		BoundEquipmentManager->OnEquipmentEquipped.AddDynamic(this, &ThisClass::OnEquipmentEquipped);
		BoundEquipmentManager->OnEquipmentUnequipped.AddDynamic(this, &ThisClass::OnEquipmentUnequipped);
		ManagerRetiredHandle = BoundEquipmentManager->OnRetired.AddUObject(this,
			&ThisClass::OnEquipmentManagerRetired, InEquipmentManager);
		const auto CurrentItems = BoundEquipmentManager->GetAllEquippedItems();
		for (auto* ItemDef : CurrentItems)
		{
			OnEquipmentEquipped(ItemDef ? ItemDef->EquipmentSlot : EProject_JEquipmentSlot::None, ItemDef);
			if (ThisBindingRevision != BindingRevision || bIsEndingPlay || IsBeingDestroyed()) return;
		}
	}
	else if (bRemote) ReconcilePublicEquipment();
}

void UProject_JEquipmentRuntimeComponent::PublishPublicSlot(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	auto* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Slot <= EProject_JEquipmentSlot::None || Slot > EProject_JEquipmentSlot::Mount) return;
	const int32 Index = PublicEquipment.Items.IndexOfByPredicate([Slot](const auto& Row) { return Row.Slot == Slot; });
	if (ItemDef)
	{
		if (Index != INDEX_NONE && PublicEquipment.Items[Index].ItemDef == ItemDef) return;
		auto& Row = Index == INDEX_NONE ? PublicEquipment.Items.AddDefaulted_GetRef() : PublicEquipment.Items[Index];
		Row.Slot = Slot; Row.ItemDef = ItemDef; PublicEquipment.MarkItemDirty(Row);
	}
	else
	{
		if (Index == INDEX_NONE) return;
		PublicEquipment.Items.RemoveAtSwap(Index); PublicEquipment.MarkArrayDirty();
	}
	if (!bIsEndingPlay && !Owner->IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown) Owner->ForceNetUpdate();
}

void UProject_JEquipmentRuntimeComponent::OnPublicEquipmentReceived()
{
	++PublicEquipmentRevision;
	if (!bIsEndingPlay && !IsBeingDestroyed() && HasBegunPlay() && UsesReplicatedPresentation())
		BindToEquipmentManager(nullptr);
}

void UProject_JEquipmentRuntimeComponent::ReconcilePublicEquipment()
{
	auto* Owner = GetOwner();
	if (bIsEndingPlay || IsBeingDestroyed() || !bUsingReplicatedPresentation || !UsesReplicatedPresentation() ||
		!IsValid(Owner) || Owner->IsActorBeingDestroyed()) return;
	const TStrongObjectPtr<UProject_JEquipmentRuntimeComponent> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(Owner);
	const uint64 SourceRevision = PublicEquipmentRevision, SourceBinding = BindingRevision;
	const auto Current = [this, Owner, SourceRevision, SourceBinding]
	{
		return !bIsEndingPlay && !IsBeingDestroyed() && !Owner->IsActorBeingDestroyed() &&
			PublicEquipmentRevision == SourceRevision && BindingRevision == SourceBinding && UsesReplicatedPresentation();
	};
	// At most one row per real equipment slot. Reconcile only complete, resolved
	// snapshots. Unresolved replacement rows revoke the old visual immediately;
	// Iris calls PostReplicatedReceive again when their definition resolves.
	if (PublicEquipment.Items.Num() > static_cast<int32>(EProject_JEquipmentSlot::Mount)) return;
	const auto Desired = PublicEquipment.Items;
	TArray<TStrongObjectPtr<UProject_JEquipmentItemDefinition>> KeepDefinitions;
	TSet<EProject_JEquipmentSlot> Seen;
	for (const auto& Row : Desired)
	{
		if (Row.Slot <= EProject_JEquipmentSlot::None || Row.Slot > EProject_JEquipmentSlot::Mount || Seen.Contains(Row.Slot)) return;
		Seen.Add(Row.Slot);
		if (IsValid(Row.ItemDef)) KeepDefinitions.Emplace(Row.ItemDef.Get());
	}
	TArray<EProject_JEquipmentSlot> Keys; RuntimeItems.GetKeys(Keys);
	for (auto Slot : Keys)
	{
		const auto* Existing = RuntimeItems.Find(Slot);
		const auto* Wanted = Desired.FindByPredicate([Slot](const auto& Row) { return Row.Slot == Slot; });
		if (Existing && (!Wanted || Existing->ItemDef != Wanted->ItemDef)) OnEquipmentUnequipped(Slot, Existing->ItemDef);
		if (!Current()) return;
	}
	for (const auto& Row : Desired)
	{
		const auto* Existing = RuntimeItems.Find(Row.Slot);
		if (IsValid(Row.ItemDef) && (!Existing || Existing->ItemDef != Row.ItemDef)) OnEquipmentEquipped(Row.Slot, Row.ItemDef);
		if (!Current()) return;
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
	PublishPublicSlot(Slot, ItemDef);
	RefreshCurrentWeaponConfiguration();

	StartLocalSpawnEquipment(Slot, ItemDef); // Visual admission never owns gameplay grants or public state.
}

void UProject_JEquipmentRuntimeComponent::OnEquipmentUnequipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	// Revoke first: cancellation callbacks must not observe or reuse this weapon.
	// Own a copy across callbacks rather than retaining a mutable map reference.
	FProject_JEquipmentRuntimeItem RuntimeItem;
	if (!RuntimeItems.RemoveAndCopyValue(Slot, RuntimeItem)) return;
	PublishPublicSlot(Slot, nullptr);
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
        FName EffectSource;
        if (ProjectJASC)
        {
            // Use the engine's weak-object incarnation, not a reusable object index
            // or per-manager path string. Zero preserves the existing null provider.
            const int32 ProviderNumber = BoundEquipmentManager
                ? GUObjectArray.AllocateSerialNumber(int32(BoundEquipmentManager->GetUniqueID())) : 0;
            // A nonnumeric suffix keeps the full definition path in the comparison
            // name when its asset/transient name itself ends in a number.
            const FName DefinitionSource(*FString::Printf(TEXT("EquipmentEffects.%d.%s.Provider"),
                int32(ItemDef.EquipmentSlot), *ItemDef.GetPathName()));
            EffectSource = FName(DefinitionSource, ProviderNumber);
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

bool UProject_JEquipmentRuntimeComponent::CanCreateEquipmentVisuals() const
{
	const UWorld* VisualWorld = GetWorld();
	const AActor* VisualOwner = GetOwner();
	return FApp::CanEverRender() && !bIsEndingPlay && !IsBeingDestroyed() && VisualWorld && !VisualWorld->bIsTearingDown &&
		IsValid(VisualOwner) && !VisualOwner->IsActorBeingDestroyed() && VisualOwner->GetNetMode() != NM_DedicatedServer;
}

void UProject_JEquipmentRuntimeComponent::StartLocalSpawnEquipment(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef)
{
	check(IsInGameThread());
	if (!CanCreateEquipmentVisuals() || !IsValid(ItemDef) || ItemDef->EquipmentMesh.IsNull()) return;
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
	if (bIsEndingPlay || IsBeingDestroyed() || !IsValid(ItemDef)) return;
	uint64 ExpectedRevision = 0, ExpectedToken = 0;
	{
		auto* Initial = RuntimeItems.Find(Slot);
		if (!Initial || Initial->ItemDef != ItemDef) return;
		if (!CanCreateEquipmentVisuals())
		{
			CancelEquipmentMeshLoad(*Initial);
			ScheduleVisualRetry();
			return;
		}
		if (Initial->SpawnedMesh) return;
		ExpectedRevision = Initial->VisualRevision;
		ExpectedToken = Initial->VisualLoadToken;
	}

	APawn* const OwnerCharacter = Cast<APawn>(GetOwner());
	UWorld* const OriginalWorld = GetWorld();
	if (!IsValid(OwnerCharacter) || !OriginalWorld) return;
	const uint64 ExpectedBinding = BindingRevision;
	const FSoftObjectPath ExpectedPath = ItemDef->EquipmentMesh.ToSoftObjectPath();
	const FName ExpectedSocket = ItemDef->AttachSocketName;
	const EProject_JEquipmentPoseMode ExpectedPoseMode = ItemDef->PoseMode;
	const bool bCastShadow = ItemDef->bCastDynamicShadow;
	const float MaxDrawDistance = ItemDef->MaxDrawDistance;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<APawn> KeepOwner(OwnerCharacter);
	const TStrongObjectPtr<UWorld> KeepWorld(OriginalWorld);
	const TStrongObjectPtr<UProject_JEquipmentItemDefinition> KeepItem(ItemDef);
	const TStrongObjectPtr<UProject_JVisualAssetSubsystem> KeepService(VisualAssets.Get());
	USkeletalMesh* const LoadedMesh = ItemDef->EquipmentMesh.Get();
	const TStrongObjectPtr<USkeletalMesh> KeepMesh(LoadedMesh);

	// Never retain a map reference over mesh/pose/registration callbacks. This
	// narrower request ownership also permits retiring our old request if mutable
	// definition metadata changed, without touching a replacement slot/token.
	const auto OwnsRequest = [&]()
	{
		const auto* Current = RuntimeItems.Find(Slot);
		return !IsBeingDestroyed() && GetOwner() == OwnerCharacter && GetWorld() == OriginalWorld &&
			BindingRevision == ExpectedBinding && Current && Current->ItemDef == ItemDef &&
			Current->VisualRevision == ExpectedRevision && Current->VisualLoadToken == ExpectedToken &&
			!Current->SpawnedMesh && VisualAssets.Get() == KeepService.Get();
	};
	const auto IsCurrent = [&]()
	{
		return OwnsRequest() && CanCreateEquipmentVisuals() && IsValid(ItemDef) &&
			(!KeepService.IsValid() || KeepService->GetWorld() == OriginalWorld) &&
			ItemDef->EquipmentMesh.ToSoftObjectPath() == ExpectedPath && ItemDef->EquipmentMesh.Get() == LoadedMesh &&
			ItemDef->AttachSocketName == ExpectedSocket && ItemDef->PoseMode == ExpectedPoseMode;
	};
	const auto ReleaseOwnRequest = [&]()
	{
		if (!OwnsRequest()) return;
		RuntimeItems.FindChecked(Slot).VisualLoadToken = 0;
		if (KeepService.IsValid()) KeepService->Release(ExpectedToken);
		ScheduleVisualRetry();
	};
	if (!LoadedMesh)
	{
		ReleaseOwnRequest();
		UE_LOG(LogProjectJEquipmentRuntime, Warning, TEXT("Equipment mesh load failed. Item=%s Path=%s"),
			*GetNameSafe(ItemDef), *ExpectedPath.ToString());
		return;
	}
	if (!IsCurrent()) { ReleaseOwnRequest(); return; }

	const auto ResolveSource = [&](FName& OutSocket)
	{
		OutSocket = ExpectedSocket;
		return ExpectedSocket.IsNone()
			? Project_J::Animation::ResolveEquipmentPoseSource(*OwnerCharacter, *LoadedMesh,
				ExpectedPoseMode == EProject_JEquipmentPoseMode::CopyPose)
			: Project_J::Animation::ResolveWeaponAttachmentMesh(*OwnerCharacter,
				ExpectedSocket, NAME_None, true, OutSocket);
	};
	FName AttachmentSocket;
	USkeletalMeshComponent* const PoseSource = ResolveSource(AttachmentSocket);
	if (!IsCurrent()) { ReleaseOwnRequest(); return; }
	if (!IsValid(PoseSource) || PoseSource->IsBeingDestroyed())
	{
		UE_LOG(LogProjectJEquipmentRuntime, Warning,
			TEXT("Equipment visual has no compatible pose source or attachment socket. Owner=%s Mesh=%s Slot=%d"),
			*GetNameSafe(OwnerCharacter), *GetNameSafe(LoadedMesh), int32(Slot));
		ReleaseOwnRequest();
		return;
	}
	const TStrongObjectPtr<USkeletalMeshComponent> KeepSource(PoseSource);
	USkeletalMesh* const SourceAsset = PoseSource->GetSkeletalMeshAsset();
	const TStrongObjectPtr<USkeletalMesh> KeepSourceAsset(SourceAsset);
	const bool bSourceRegistered = PoseSource->IsRegistered();
	const auto SourceIsCurrent = [&]()
	{
		return IsCurrent() && IsValid(PoseSource) && !PoseSource->IsBeingDestroyed() &&
			PoseSource->GetOwner() == OwnerCharacter && PoseSource->GetWorld() == OriginalWorld &&
			PoseSource->IsRegistered() == bSourceRegistered && PoseSource->GetSkeletalMeshAsset() == SourceAsset;
	};

	const TStrongObjectPtr<UProject_JModularMeshComponent> Candidate(NewObject<UProject_JModularMeshComponent>(OwnerCharacter));
	const auto CandidateIsCurrent = [&]()
	{
		return SourceIsCurrent() && Candidate.IsValid() && !Candidate->IsBeingDestroyed() &&
			Candidate->GetOwner() == OwnerCharacter && Candidate->GetWorld() == OriginalWorld;
	};
	const auto DiscardCandidate = [&]()
	{
		if (Candidate.IsValid() && !Candidate->IsBeingDestroyed()) Candidate->DestroyComponent();
		// Destruction is another callback boundary; re-find only our exact old request.
		ReleaseOwnRequest();
	};
	if (!CandidateIsCurrent()) { DiscardCandidate(); return; }
	Candidate->SetSkeletalMesh(LoadedMesh);
	if (!CandidateIsCurrent() || Candidate->GetSkeletalMeshAsset() != LoadedMesh) { DiscardCandidate(); return; }
	Candidate->SetCastShadow(bCastShadow);
	Candidate->SetCullDistance(MaxDrawDistance);
	if (!CandidateIsCurrent()) { DiscardCandidate(); return; }

	if (ExpectedSocket.IsNone())
	{
		if (ExpectedPoseMode == EProject_JEquipmentPoseMode::CopyPose)
		{
			if (!Candidate->AttachAndCopyPose(PoseSource))
			{
				DiscardCandidate();
				UE_LOG(LogProjectJEquipmentRuntime, Warning, TEXT("Copy-pose equipment has an incompatible base hierarchy. Item=%s"), *GetNameSafe(ItemDef));
				return;
			}
		}
		else Candidate->AttachAndSetLeader(PoseSource);
	}
	else
	{
		Candidate->AttachToComponent(PoseSource, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachmentSocket);
		if (!CandidateIsCurrent()) { DiscardCandidate(); return; }
		Candidate->SetLeaderPoseComponent(nullptr);
	}
	const auto AttachmentIsCurrent = [&]()
	{
		if (!CandidateIsCurrent() || Candidate->GetSkeletalMeshAsset() != LoadedMesh ||
			Candidate->GetAttachParent() != PoseSource || Candidate->GetAttachSocketName() != AttachmentSocket) return false;
		if (!ExpectedSocket.IsNone()) return !Candidate->bFollowAttachParentPose && !Candidate->LeaderPoseComponent.IsValid();
		return ExpectedPoseMode == EProject_JEquipmentPoseMode::CopyPose
			? !Candidate->bFollowAttachParentPose && !Candidate->LeaderPoseComponent.IsValid() &&
				Candidate->GetAnimClass() == UProject_JCopyPoseAnimInstance::StaticClass()
			: Candidate->bFollowAttachParentPose && Candidate->LeaderPoseComponent.Get() == PoseSource;
	};
	if (!AttachmentIsCurrent()) { DiscardCandidate(); return; }
	// Set asset and source before native registration initializes cloth or animation.
	Candidate->RegisterComponent();
	if (!AttachmentIsCurrent() || !Candidate->IsRegistered()) { DiscardCandidate(); return; }
	// Source identity may have stayed live while a callback installed a different
	// follower/socket winner. Resolve once more only at the publication boundary.
	FName CurrentSocket;
	USkeletalMeshComponent* const CurrentSource = ResolveSource(CurrentSocket);
	if (!AttachmentIsCurrent() || !Candidate->IsRegistered() || CurrentSource != PoseSource || CurrentSocket != AttachmentSocket)
	{ DiscardCandidate(); return; }

	// The registered component's reflected SkinnedAsset now owns residency. Detach
	// the request before Release; no callback may see this slot's delivered lease.
	auto* Current = RuntimeItems.Find(Slot);
	Current->SpawnedMesh = Candidate.Get();
	Current->VisualLoadToken = 0;
	if (KeepService.IsValid()) KeepService->Release(ExpectedToken);
}

void UProject_JEquipmentRuntimeComponent::CancelEquipmentMeshLoad(FProject_JEquipmentRuntimeItem& RuntimeItem) const
{
	check(IsInGameThread());
	if (RuntimeItem.VisualLoadToken)
	{
		const uint64 PreviousToken = RuntimeItem.VisualLoadToken;
		RuntimeItem.VisualLoadToken = 0;
		if (auto* Service = VisualAssets.Get()) { Service->Release(PreviousToken); }
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
	if (!CanCreateEquipmentVisuals())
	{
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(VisualRetryTimer);
		return;
	}
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
	if (!CanCreateEquipmentVisuals()) { return; }
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
	// The existing Pawn pair RepNotifies are the sole remote style/profile writer.
	// Public item deltas must not race or overwrite that separate replicated pair.
	if (!OwnerPlayer || UsesReplicatedPresentation()) return;

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
