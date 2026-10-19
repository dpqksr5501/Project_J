#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"

#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "GameplayEffect.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

void FProject_JInventoryArray::PostReplicatedAdd(const TArrayView<int32> &AddedIndices, int32 FinalSize)
{
	for (const int32 Index : AddedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemAdded(Items[Index]);
		}
	}
}

void FProject_JInventoryArray::PostReplicatedChange(const TArrayView<int32> &ChangedIndices, int32 FinalSize)
{
	for (const int32 Index : ChangedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemChanged(Items[Index]);
		}
	}
}

void FProject_JInventoryArray::PreReplicatedRemove(const TArrayView<int32> &RemovedIndices, int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemRemoved(Items[Index]);
		}
	}
}

UProject_JInventoryComponent::UProject_JInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	InventoryArray.OwnerComponent = this;
}

TArray<FProject_JItemInstanceData> UProject_JInventoryComponent::GetItemInstances() const
{
	TArray<FProject_JItemInstanceData> Result;
	Result.Reserve(InventoryArray.Items.Num());
	for (const auto &Item : InventoryArray.Items)
		Result.Add(Item.ItemInstance);
	return Result;
}

void UProject_JInventoryComponent::BeginPlay()
{
	bEnding = false;
	InventoryArray.OwnerComponent = this;
	Super::BeginPlay();
}
bool UProject_JInventoryComponent::ValidatePersistentItems(const TArray<FProject_JItemInstanceData> &Items,
														   bool bTrustedApply) const
{
	const bool Admitted = bTrustedApply ? (CanPublishState() && GetOwner()->HasAuthority()) : HasMutationAuthority();
	if (!Admitted || Items.Num() > 4096)
		return false;
	TSet<FGuid> Ids;
	TSet<int32> Orders;
	for (const auto &Item : Items)
	{
		if (!Item.IsValid() || Item.ItemDef->ItemId.IsNone() || Item.ItemDef->SchemaVersion < 1 ||
			Item.StackCount > Item.ItemDef->MaxStackCount || Item.ItemLevel < 1 || Item.ItemLevel > 10000 ||
			Item.BagOrder < 0 || Item.BagOrder == MAX_int32 || Ids.Contains(Item.InstanceId) ||
			Orders.Contains(Item.BagOrder) || Item.bIsLocked != Item.bIsEquipped)
			return false;
		Ids.Add(Item.InstanceId);
		Orders.Add(Item.BagOrder);
	}
	return true;
}
void UProject_JInventoryComponent::InstallPersistentItems(const TArray<FProject_JItemInstanceData> &Items)
{
	check(IsInGameThread());
	check(ValidatePersistentItems(Items, true));
	InventoryArray.Items.Reset();
	for (const auto &Item : Items)
	{
		auto &Entry = InventoryArray.Items.AddDefaulted_GetRef();
		Entry.ItemInstance = Item;
		InventoryArray.MarkItemDirty(Entry);
	}
	InventoryArray.MarkArrayDirty();
}
void UProject_JInventoryComponent::PublishPersistentItems(const TArray<FProject_JItemInstanceData> &Before)
{
	const auto After = GetItemInstances();
	for (const auto &Item : Before)
		if (!After.ContainsByPredicate([&](const auto &V) { return V.InstanceId == Item.InstanceId; }))
		{
			if (!CanPublishState())
				return;
			OnItemRemoved.Broadcast(Item);
		}
	for (const auto &Item : After)
	{
		if (!CanPublishState())
			return;
		const auto *Old = Before.FindByPredicate([&](const auto &V) { return V.InstanceId == Item.InstanceId; });
		if (!Old)
			OnItemAdded.Broadcast(Item);
		else if (Old->ItemDef != Item.ItemDef || Old->ItemLevel != Item.ItemLevel ||
				 Old->StackCount != Item.StackCount || Old->BagOrder != Item.BagOrder ||
				 Old->bIsLocked != Item.bIsLocked || Old->bIsEquipped != Item.bIsEquipped)
			OnItemChanged.Broadcast(Item);
	}
	if (CanPublishState())
		GetOwner()->ForceNetUpdate();
}

void UProject_JInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Retire();
	Super::EndPlay(Reason);
}

void UProject_JInventoryComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Retire();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UProject_JInventoryComponent::CanPublishState() const
{
	return !bEnding && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
		   GetWorld() && !GetWorld()->bIsTearingDown;
}

bool UProject_JInventoryComponent::HasMutationAuthority() const
{
	return CanPublishState() && GetOwner()->HasAuthority() && (!PersistentAdmission || PersistentAdmission());
}

void UProject_JInventoryComponent::Retire()
{
	bEnding = true;
	InventoryArray.OwnerComponent = nullptr;
	OnItemAdded.Clear();
	OnItemChanged.Clear();
	OnItemRemoved.Clear();
	OnUseCompleted.Clear();
	OnBagCompleted.Clear();
}

void UProject_JInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JInventoryComponent, InventoryArray, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UProject_JInventoryComponent, NextUseTime, COND_OwnerOnly);
}

FProject_JItemInstanceData UProject_JInventoryComponent::AddItemDefinition(UProject_JItemDefinition *ItemDef,
																		   int32 StackCount, int32 ItemLevel)
{
	check(IsInGameThread());
	FProject_JItemInstanceData NewItem;
	NewItem.InstanceId = FGuid::NewGuid();
	NewItem.ItemDef = ItemDef;
	NewItem.StackCount = FMath::Max(1, StackCount);
	NewItem.ItemLevel = FMath::Max(1, ItemLevel);
	NewItem.BagOrder = NextBagOrder();

	if (!HasMutationAuthority() || bEditingBag || NewItem.BagOrder == INDEX_NONE || !CanCommitItemInstance(NewItem))
	{
		return FProject_JItemInstanceData();
	}
	if (SubmitPersistentEdit)
	{
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Add;
		Edit.Request = NewRequestId();
		Edit.Instance = NewItem.InstanceId;
		Edit.Definition = ItemDef;
		Edit.Amount = StackCount;
		Edit.Level = ItemLevel;
		SubmitPersistentEdit(Edit, {});
		return {}; // Only OnItemAdded confirms ownership after commit.
	}

	FProject_JInventoryArrayItem &AddedItem = InventoryArray.Items.Add_GetRef(FProject_JInventoryArrayItem());
	AddedItem.ItemInstance = NewItem;
	InventoryArray.MarkItemDirty(AddedItem);
	// Listeners may mutate the array. Publish a value snapshot after commit.
	OnItemAdded.Broadcast(NewItem);
	return NewItem;
}

bool UProject_JInventoryComponent::RemoveItemInstance(FGuid InstanceId)
{
	check(IsInGameThread());
	if (!HasMutationAuthority() || bEditingBag)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	if (!CanRemoveItemInstance(InstanceId, 1))
	{
		return false;
	}

	const FProject_JInventoryArrayItem RemovedItem = InventoryArray.Items[FoundIndex];
	if (SubmitPersistentEdit)
	{
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Remove;
		Edit.Request = NewRequestId();
		Edit.Instance = InstanceId;
		return SubmitPersistentEdit(Edit, {});
	}
	InventoryArray.Items.RemoveAt(FoundIndex);
	InventoryArray.MarkArrayDirty();
	OnItemRemoved.Broadcast(RemovedItem.ItemInstance);
	return true;
}

bool UProject_JInventoryComponent::SetItemStackCount(FGuid InstanceId, int32 NewStackCount)
{
	check(IsInGameThread());
	if (!HasMutationAuthority() || bEditingBag || NewStackCount < 0)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	FProject_JInventoryArrayItem &Item = InventoryArray.Items[FoundIndex];
	const int32 MaxStackCount = Item.ItemInstance.ItemDef ? FMath::Max(1, Item.ItemInstance.ItemDef->MaxStackCount) : 1;
	if (NewStackCount > MaxStackCount)
	{
		return false;
	}

	if (NewStackCount > Item.ItemInstance.StackCount && Item.ItemInstance.bIsLocked)
	{
		return false;
	}

	if (NewStackCount < Item.ItemInstance.StackCount &&
		!CanRemoveItemInstance(InstanceId, Item.ItemInstance.StackCount - NewStackCount))
	{
		return false;
	}

	if (SubmitPersistentEdit)
	{
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Stack;
		Edit.Request = NewRequestId();
		Edit.Instance = InstanceId;
		Edit.Amount = NewStackCount;
		return SubmitPersistentEdit(Edit, {});
	}
	if (NewStackCount == 0)
	{
		const FProject_JInventoryArrayItem RemovedItem = Item;
		InventoryArray.Items.RemoveAt(FoundIndex);
		InventoryArray.MarkArrayDirty();
		OnItemRemoved.Broadcast(RemovedItem.ItemInstance);
		return true;
	}

	Item.ItemInstance.StackCount = NewStackCount;
	InventoryArray.MarkItemDirty(Item);
	const FProject_JItemInstanceData Snapshot = Item.ItemInstance;
	OnItemChanged.Broadcast(Snapshot);
	return true;
}

bool UProject_JInventoryComponent::AddItemStackCount(FGuid InstanceId, int32 DeltaStackCount)
{
	check(IsInGameThread());
	if (!HasMutationAuthority())
	{
		return false;
	}

	if (DeltaStackCount == 0)
	{
		return HasItemInstance(InstanceId);
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CurrentStackCount = InventoryArray.Items[FoundIndex].ItemInstance.StackCount;
	const int64 NewStackCount = static_cast<int64>(CurrentStackCount) + DeltaStackCount;
	return NewStackCount >= 0 && NewStackCount <= MAX_int32 &&
		   SetItemStackCount(InstanceId, static_cast<int32>(NewStackCount));
}

bool UProject_JInventoryComponent::ConsumeItemStack(FGuid InstanceId, int32 CountToConsume)
{
	check(IsInGameThread());
	// Positive consumption is a decrement through the common checked mutator.
	// Reject before negation (including MIN_int32); zero is not a lookup request.
	return CountToConsume > 0 && AddItemStackCount(InstanceId, -CountToConsume);
}

bool UProject_JInventoryComponent::SetItemInstanceLocked(FGuid InstanceId, bool bLocked, bool bEquipped)
{
	check(IsInGameThread());
	if (SubmitPersistentEdit)
		return false; // Persistent equipment changes both owners in one command.
	if (!HasMutationAuthority() || bEditingBag)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	FProject_JInventoryArrayItem &Item = InventoryArray.Items[FoundIndex];
	Item.ItemInstance.bIsLocked = bLocked;
	Item.ItemInstance.bIsEquipped = bLocked && bEquipped;
	InventoryArray.MarkItemDirty(Item);
	const FProject_JItemInstanceData Snapshot = Item.ItemInstance;
	OnItemChanged.Broadcast(Snapshot);
	return true;
}

bool UProject_JInventoryComponent::HasItemInstance(FGuid InstanceId) const
{
	return FindItemIndex(InstanceId) != INDEX_NONE;
}

bool UProject_JInventoryComponent::IsItemInstanceLocked(FGuid InstanceId) const
{
	const int32 FoundIndex = FindItemIndex(InstanceId);
	return FoundIndex != INDEX_NONE && InventoryArray.Items[FoundIndex].ItemInstance.bIsLocked;
}

bool UProject_JInventoryComponent::CanRemoveItemInstance(FGuid InstanceId, int32 Count) const
{
	if (Count <= 0)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	const FProject_JItemInstanceData &ItemInstance = InventoryArray.Items[FoundIndex].ItemInstance;
	return ItemInstance.IsValid() && !ItemInstance.bIsLocked && ItemInstance.StackCount >= Count;
}

bool UProject_JInventoryComponent::CanMoveItemInstance(FGuid InstanceId, int32 Count) const
{
	return CanRemoveItemInstance(InstanceId, Count);
}

bool UProject_JInventoryComponent::FindItemInstance(FGuid InstanceId, FProject_JItemInstanceData &OutItemInstance) const
{
	OutItemInstance = FProject_JItemInstanceData();
	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	OutItemInstance = InventoryArray.Items[FoundIndex].ItemInstance;
	return true;
}

FString UProject_JInventoryComponent::GetReplicationDiagnosticSummary() const
{
	// ArrayReplicationKey is transport bookkeeping and can legitimately differ between server/client.
	// Keep it visible in the summary, but exclude it from the payload-equivalence fingerprint.
	uint32 StateHash = 0;
	for (const FProject_JInventoryArrayItem &Item : InventoryArray.Items)
	{
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.InstanceId));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.StackCount));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.ItemLevel));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.BagOrder));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.bIsLocked));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.bIsEquipped));
		StateHash = HashCombineFast(StateHash, GetTypeHash(GetNameSafe(Item.ItemInstance.ItemDef)));
	}

	return FString::Printf(TEXT("Items=%d ArrayKey=%d StateHash=%08X"), InventoryArray.Items.Num(),
						   InventoryArray.ArrayReplicationKey, StateHash);
}

FString UProject_JInventoryComponent::GetReplicationDiagnosticDeltaSummary() const
{
#if !UE_BUILD_SHIPPING
	return FString::Printf(TEXT("Add=%d Change=%d Remove=%d"), ReplicationDiagnosticAddedCount,
						   ReplicationDiagnosticChangedCount, ReplicationDiagnosticRemovedCount);
#else
	return TEXT("Unavailable");
#endif
}

void UProject_JInventoryComponent::ResetReplicationDiagnosticDeltaCounters()
{
#if !UE_BUILD_SHIPPING
	ReplicationDiagnosticAddedCount = 0;
	ReplicationDiagnosticChangedCount = 0;
	ReplicationDiagnosticRemovedCount = 0;
#endif
}

void UProject_JInventoryComponent::HandleReplicatedItemAdded(const FProject_JInventoryArrayItem &Item)
{
	if (!CanPublishState())
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticAddedCount;
#endif
	const FProject_JItemInstanceData Snapshot = Item.ItemInstance;
	OnItemAdded.Broadcast(Snapshot);
}

void UProject_JInventoryComponent::HandleReplicatedItemChanged(const FProject_JInventoryArrayItem &Item)
{
	if (!CanPublishState())
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticChangedCount;
#endif
	const FProject_JItemInstanceData Snapshot = Item.ItemInstance;
	OnItemChanged.Broadcast(Snapshot);
}

void UProject_JInventoryComponent::HandleReplicatedItemRemoved(const FProject_JInventoryArrayItem &Item)
{
	if (!CanPublishState())
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticRemovedCount;
#endif
	const FProject_JItemInstanceData Snapshot = Item.ItemInstance;
	OnItemRemoved.Broadcast(Snapshot);
}

bool UProject_JInventoryComponent::CanCommitItemInstance(const FProject_JItemInstanceData &ItemInstance) const
{
	if (!ItemInstance.ItemDef)
	{
		return false;
	}

	const int32 MaxStackCount = FMath::Max(1, ItemInstance.ItemDef->MaxStackCount);
	return ItemInstance.InstanceId.IsValid() && ItemInstance.StackCount > 0 &&
		   ItemInstance.StackCount <= MaxStackCount && FindItemIndex(ItemInstance.InstanceId) == INDEX_NONE;
}

int32 UProject_JInventoryComponent::FindItemIndex(FGuid InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < InventoryArray.Items.Num(); ++Index)
	{
		if (InventoryArray.Items[Index].ItemInstance.InstanceId == InstanceId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

float UProject_JInventoryComponent::GetUseCooldownRemaining() const
{
	const auto *World = GetWorld();
	const auto *GS = World ? World->GetGameState() : nullptr;
	const double Now = GS ? GS->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0;
	return FMath::Max(0., NextUseTime - Now);
}
void UProject_JInventoryComponent::RequestUseItem(FGuid RequestId, FGuid InstanceId)
{
	if (CanPublishState() && RequestId.IsValid() && InstanceId.IsValid())
		ServerUseItem(RequestId, InstanceId);
}
void UProject_JInventoryComponent::ServerUseItem_Implementation(FGuid RequestId, FGuid InstanceId)
{
	if (!HasMutationAuthority() || !RequestId.IsValid())
		return;
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner()))
	{
		ClientUseCompleted(RequestId, EProject_JItemUseResult::RateLimited);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - UseRequestWindow >= 1 || UseRequestWindow < 0)
	{
		UseRequestWindow = Now;
		UseRequestCount = 0;
	}
	if (++UseRequestCount > 20)
	{
		ClientUseCompleted(RequestId, EProject_JItemUseResult::RateLimited);
		return;
	}
	if (SubmitPersistentEdit)
	{
		ClientUseCompleted(RequestId, SubmitPersistentUse(RequestId, InstanceId));
		return;
	}
	if (const auto *Receipt = UseReceipts.Find(RequestId))
	{
		ClientUseCompleted(RequestId,
						   Receipt->InstanceId == InstanceId ? Receipt->Result : EProject_JItemUseResult::Invalid);
		return;
	}
	const auto Result = TryUseItem(InstanceId);
	if (!CanPublishState())
		return;
	if (UseReceiptOrder.Num() >= 64)
	{
		UseReceipts.Remove(UseReceiptOrder[0]);
		UseReceiptOrder.RemoveAt(0);
	}
	UseReceiptOrder.Add(RequestId);
	UseReceipts.Add(RequestId, FUseReceipt{InstanceId, Result});
	ClientUseCompleted(RequestId, Result);
}
void UProject_JInventoryComponent::ClientUseCompleted_Implementation(FGuid RequestId, EProject_JItemUseResult Result)
{
	if (CanPublishState())
		OnUseCompleted.Broadcast(RequestId, Result);
}
EProject_JItemUseResult UProject_JInventoryComponent::TryUseItem(FGuid InstanceId)
{
	if (!HasMutationAuthority() || bUsingItem || bEditingBag)
		return EProject_JItemUseResult::Unavailable;
	if (SubmitPersistentEdit)
		return SubmitPersistentUse(NewRequestId(), InstanceId);
	FProject_JItemInstanceData Item;
	if (!FindItemInstance(InstanceId, Item))
		return EProject_JItemUseResult::Invalid;
	if (Item.bIsLocked || Item.bIsEquipped)
		return EProject_JItemUseResult::Locked;
	const auto *Def = Cast<UProject_JConsumableDefinition>(Item.ItemDef);
	const auto *Interface = Cast<IAbilitySystemInterface>(GetOwner());
	auto *ASC = Interface ? Interface->GetAbilitySystemComponent() : nullptr;
	if (!Def || !ASC || !ASC->HasAttributeSetForAttribute(UProject_JAttributeSet::GetHealthAttribute()) ||
		!ASC->HasAttributeSetForAttribute(UProject_JAttributeSet::GetManaAttribute()) ||
		!FMath::IsFinite(Def->RestoreHealth) || !FMath::IsFinite(Def->RestoreMana) || Def->RestoreHealth < 0 ||
		Def->RestoreMana < 0 || !FMath::IsFinite(Def->CooldownSeconds) || Def->CooldownSeconds < 0.1f ||
		Def->CooldownSeconds > 3600)
		return EProject_JItemUseResult::Invalid;
	if (ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) <= 0)
		return EProject_JItemUseResult::Unavailable;
	if (GetUseCooldownRemaining() > 0)
		return EProject_JItemUseResult::Cooldown;
	const float Health = FMath::Clamp(ASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute()) -
										  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),
									  0.f, Def->RestoreHealth);
	const float Mana = FMath::Clamp(ASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxManaAttribute()) -
										ASC->GetNumericAttribute(UProject_JAttributeSet::GetManaAttribute()),
									0.f, Def->RestoreMana);
	if (!FMath::IsFinite(Health) || !FMath::IsFinite(Mana) || (Health <= 0 && Mana <= 0))
		return EProject_JItemUseResult::NoEffect;
	TGuardValue<bool> Guard(bUsingItem, true);
	const TStrongObjectPtr<UProject_JInventoryComponent> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	const TStrongObjectPtr<UAbilitySystemComponent> KeepASC(ASC);
	const TStrongObjectPtr<AActor> OriginalAvatar(ASC->GetAvatarActor());
	const TStrongObjectPtr<AActor> OriginalASCOwner(ASC->GetOwnerActor());
	const float CooldownSeconds = Def->CooldownSeconds;
	auto *Effect = NewObject<UGameplayEffect>();
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
	const TStrongObjectPtr<UGameplayEffect> KeepEffect(Effect);
	auto Add = [Effect](FGameplayAttribute Attribute, float Amount) {
		if (Amount > 0)
		{
			FGameplayModifierInfo Modifier;
			Modifier.Attribute = Attribute;
			Modifier.ModifierOp = EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = FScalableFloat(Amount);
			Effect->Modifiers.Add(Modifier);
		}
	};
	Add(UProject_JAttributeSet::GetHealthAttribute(), Health);
	Add(UProject_JAttributeSet::GetManaAttribute(), Mana);
	FGameplayEffectSpec Spec(Effect, ASC->MakeEffectContext(), 1.f);
	if (!CanRemoveItemInstance(InstanceId, 1) || !ConsumeItemStack(InstanceId, 1))
		return EProject_JItemUseResult::Unavailable;
	// Consumption was already published. Do not recreate an observed item when
	// its recipient retires; report Unavailable without healing or a cooldown.
	const auto *CurrentInterface = Cast<IAbilitySystemInterface>(GetOwner());
	if (!HasMutationAuthority() || GetOwner() != KeepOwner.Get() || !CurrentInterface ||
		CurrentInterface->GetAbilitySystemComponent() != KeepASC.Get() || !IsValid(ASC) || ASC->IsBeingDestroyed() ||
		ASC->GetAvatarActor() != OriginalAvatar.Get() || ASC->GetOwnerActor() != OriginalASCOwner.Get())
		return EProject_JItemUseResult::Unavailable;
	if ((OriginalAvatar.Get() && (!IsValid(OriginalAvatar.Get()) || OriginalAvatar->IsActorBeingDestroyed())) ||
		(OriginalASCOwner.Get() && (!IsValid(OriginalASCOwner.Get()) || OriginalASCOwner->IsActorBeingDestroyed())))
		return EProject_JItemUseResult::Unavailable;
	const float CurrentHealth = ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
	if (!FMath::IsFinite(CurrentHealth) || CurrentHealth <= 0)
		return EProject_JItemUseResult::Unavailable;
	// Reserve shared recovery cooldown before broadcasting GAS attribute events (reentrancy).
	NextUseTime = GetWorld()->GetTimeSeconds() + CooldownSeconds;
	ASC->ApplyGameplayEffectSpecToSelf(Spec);
	if (CanPublishState())
		GetOwner()->ForceNetUpdate();
	return EProject_JItemUseResult::Success;
}

void UProject_JInventoryComponent::OnRep_NextUseTime()
{
	if (CanPublishState())
		OnItemChanged.Broadcast(FProject_JItemInstanceData());
}

int32 UProject_JInventoryComponent::NextBagOrder() const
{
	int64 Next = 0;
	for (const auto &Entry : InventoryArray.Items)
		Next = FMath::Max(Next, static_cast<int64>(Entry.ItemInstance.BagOrder) + 1);
	return Next < MAX_int32 ? static_cast<int32>(Next) : INDEX_NONE;
}
void UProject_JInventoryComponent::RequestBagChange(FGuid RequestId, const FProject_JBagRequest &Request)
{
	if (CanPublishState() && RequestId.IsValid())
		ServerBagChange(RequestId, Request);
}
void UProject_JInventoryComponent::ServerBagChange_Implementation(FGuid RequestId, FProject_JBagRequest Request)
{
	if (!HasMutationAuthority() || !RequestId.IsValid())
		return;
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner()))
	{
		ClientBagCompleted(RequestId, EProject_JBagResult::RateLimited);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (BagRequestWindow < 0 || Now - BagRequestWindow >= 1)
	{
		BagRequestWindow = Now;
		BagRequestCount = 0;
	}
	if (++BagRequestCount > 20)
	{
		ClientBagCompleted(RequestId, EProject_JBagResult::RateLimited);
		return;
	}
	if (SubmitPersistentEdit)
	{
		ClientBagCompleted(RequestId, SubmitPersistentBag(RequestId, Request));
		return;
	}
	if (const auto *Receipt = BagReceipts.Find(RequestId))
	{
		ClientBagCompleted(RequestId, Receipt->Request == Request ? Receipt->Result : EProject_JBagResult::Invalid);
		return;
	}
	const auto Result = TryBagChange(Request);
	if (!CanPublishState())
		return;
	if (BagReceiptOrder.Num() >= 64)
	{
		BagReceipts.Remove(BagReceiptOrder[0]);
		BagReceiptOrder.RemoveAt(0);
	}
	BagReceiptOrder.Add(RequestId);
	BagReceipts.Add(RequestId, FBagReceipt{Request, Result});
	ClientBagCompleted(RequestId, Result);
}
void UProject_JInventoryComponent::ClientBagCompleted_Implementation(FGuid RequestId, EProject_JBagResult Result)
{
	if (CanPublishState())
		OnBagCompleted.Broadcast(RequestId, Result);
}
EProject_JBagResult UProject_JInventoryComponent::TryBagChange(const FProject_JBagRequest &Request)
{
	check(IsInGameThread());
	if (!HasMutationAuthority() || bEditingBag || bUsingItem)
		return EProject_JBagResult::Unavailable;
	if (SubmitPersistentEdit)
		return SubmitPersistentBag(NewRequestId(), Request);
	const int32 SourceIndex = FindItemIndex(Request.Source);
	if (SourceIndex == INDEX_NONE)
		return EProject_JBagResult::Invalid;
	const auto Source = InventoryArray.Items[SourceIndex].ItemInstance;
	if (!Source.IsValid())
		return EProject_JBagResult::Invalid;
	if (Source.bIsLocked || Source.bIsEquipped)
		return EProject_JBagResult::Locked;
	if (Source.StackCount != Request.ExpectedSourceCount || Source.BagOrder != Request.ExpectedSourceOrder)
		return EProject_JBagResult::Changed;
	if (Request.Operation == EProject_JBagOperation::Split)
	{
		if (Request.Target.IsValid() || Request.Count <= 0 || Request.Count >= Source.StackCount ||
			Source.ItemDef->MaxStackCount <= 1)
			return EProject_JBagResult::Invalid;
		if (InventoryArray.Items.Num() >= 4096 || NextBagOrder() == INDEX_NONE)
			return EProject_JBagResult::Full;
		TGuardValue<bool> Guard(bEditingBag, true);
		auto NewItem = Source;
		NewItem.InstanceId = FGuid::NewGuid();
		NewItem.StackCount = Request.Count;
		NewItem.BagOrder = NextBagOrder();
		auto &Original = InventoryArray.Items[SourceIndex];
		Original.ItemInstance.StackCount -= Request.Count;
		InventoryArray.MarkItemDirty(Original);
		const auto OriginalSnapshot = Original.ItemInstance;
		auto &Added = InventoryArray.Items.AddDefaulted_GetRef();
		Added.ItemInstance = NewItem;
		InventoryArray.MarkItemDirty(Added);
		// Both sides are committed before observers run. No array reference survives a callback.
		OnItemChanged.Broadcast(OriginalSnapshot);
		OnItemAdded.Broadcast(NewItem);
		GetOwner()->ForceNetUpdate();
		return EProject_JBagResult::Success;
	}
	const int32 TargetIndex = FindItemIndex(Request.Target);
	if (TargetIndex == INDEX_NONE || TargetIndex == SourceIndex)
		return EProject_JBagResult::Invalid;
	const auto Target = InventoryArray.Items[TargetIndex].ItemInstance;
	if (!Target.IsValid())
		return EProject_JBagResult::Invalid;
	if (Target.bIsLocked || Target.bIsEquipped)
		return EProject_JBagResult::Locked;
	if (Target.StackCount != Request.ExpectedTargetCount || Target.BagOrder != Request.ExpectedTargetOrder)
		return EProject_JBagResult::Changed;
	if (Request.Operation == EProject_JBagOperation::Merge)
	{
		if (Source.ItemDef != Target.ItemDef || Source.ItemLevel != Target.ItemLevel ||
			Source.ItemDef->MaxStackCount <= 1)
			return EProject_JBagResult::Incompatible;
		const int64 Space = static_cast<int64>(Target.ItemDef->MaxStackCount) - Target.StackCount;
		if (Space <= 0)
			return EProject_JBagResult::Full;
		if (Request.Count <= 0 || Request.Count > Source.StackCount || Request.Count > Space)
			return EProject_JBagResult::Changed;
		TGuardValue<bool> Guard(bEditingBag, true);
		auto &Destination = InventoryArray.Items[TargetIndex];
		Destination.ItemInstance.StackCount += Request.Count;
		InventoryArray.MarkItemDirty(Destination);
		const auto TargetSnapshot = Destination.ItemInstance;
		auto &Original = InventoryArray.Items[SourceIndex];
		Original.ItemInstance.StackCount -= Request.Count;
		const auto SourceSnapshot = Original.ItemInstance;
		const bool bRemoved = Original.ItemInstance.StackCount == 0;
		if (bRemoved)
		{
			InventoryArray.Items.RemoveAt(SourceIndex);
			InventoryArray.MarkArrayDirty();
		}
		else
			InventoryArray.MarkItemDirty(Original);
		OnItemChanged.Broadcast(TargetSnapshot);
		if (bRemoved)
			OnItemRemoved.Broadcast(Source);
		else
			OnItemChanged.Broadcast(SourceSnapshot);
		GetOwner()->ForceNetUpdate();
		return EProject_JBagResult::Success;
	}
	if (Request.Operation != EProject_JBagOperation::Swap || Request.Count != 0 || Source.BagOrder < 0 ||
		Target.BagOrder < 0 || Source.BagOrder == Target.BagOrder)
		return EProject_JBagResult::Invalid;
	TGuardValue<bool> Guard(bEditingBag, true);
	auto &Original = InventoryArray.Items[SourceIndex];
	auto &Destination = InventoryArray.Items[TargetIndex];
	Swap(Original.ItemInstance.BagOrder, Destination.ItemInstance.BagOrder);
	InventoryArray.MarkItemDirty(Original);
	InventoryArray.MarkItemDirty(Destination);
	const auto SourceSnapshot = Original.ItemInstance;
	const auto TargetSnapshot = Destination.ItemInstance;
	OnItemChanged.Broadcast(SourceSnapshot);
	OnItemChanged.Broadcast(TargetSnapshot);
	GetOwner()->ForceNetUpdate();
	return EProject_JBagResult::Success;
}
EProject_JBagResult UProject_JInventoryComponent::SubmitPersistentBag(FGuid RequestId, const FProject_JBagRequest &R)
{
	FProject_JPersistentEdit E;
	E.Kind = EProject_JPersistentEdit::Bag;
	E.Request = RequestId;
	E.Instance = R.Source;
	E.Target = R.Target;
	E.Amount = R.Count;
	E.Operation = int32(R.Operation);
	E.SourceCount = R.ExpectedSourceCount;
	E.TargetCount = R.ExpectedTargetCount;
	E.SourceOrder = R.ExpectedSourceOrder;
	E.TargetOrder = R.ExpectedTargetOrder;
	TWeakObjectPtr<UProject_JInventoryComponent> WeakThis(this);
	return SubmitPersistentEdit(E,
								[WeakThis, RequestId](bool OK, bool) {
									if (auto *Self = WeakThis.Get(); Self && Self->CanPublishState())
										Self->ClientBagCompleted(RequestId, OK ? EProject_JBagResult::Success
																			   : EProject_JBagResult::Unavailable);
								})
			   ? EProject_JBagResult::Pending
			   : EProject_JBagResult::Unavailable;
}
bool UProject_JInventoryComponent::CanUsePersistentItem(FGuid Id) const
{
	FProject_JItemInstanceData Item;
	if (!FindItemInstance(Id, Item) || Item.bIsLocked || Item.bIsEquipped)
		return false;
	const auto *D = Cast<UProject_JConsumableDefinition>(Item.ItemDef);
	const auto *I = Cast<IAbilitySystemInterface>(GetOwner());
	auto *A = I ? I->GetAbilitySystemComponent() : nullptr;
	const auto *BindingASC = Cast<UProject_JAbilitySystemComponent>(A);
	if (!BindingASC || BindingASC->GetActorInfoRevision() == 0 || A->GetOwnerActor() != GetOwner())
		return false;
	if (!D || !A || !A->HasAttributeSetForAttribute(UProject_JAttributeSet::GetHealthAttribute()) ||
		!A->HasAttributeSetForAttribute(UProject_JAttributeSet::GetManaAttribute()) ||
		!FMath::IsFinite(D->RestoreHealth) || !FMath::IsFinite(D->RestoreMana) || D->RestoreHealth < 0 ||
		D->RestoreMana < 0 || !FMath::IsFinite(D->CooldownSeconds) || D->CooldownSeconds < .1f ||
		D->CooldownSeconds > 3600 || GetUseCooldownRemaining() > 0)
		return false;
	const float Health = A->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),
				Mana = A->GetNumericAttribute(UProject_JAttributeSet::GetManaAttribute()),
				MaxHealth = A->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute()),
				MaxMana = A->GetNumericAttribute(UProject_JAttributeSet::GetMaxManaAttribute());
	return FMath::IsFinite(Health) && FMath::IsFinite(Mana) && FMath::IsFinite(MaxHealth) && FMath::IsFinite(MaxMana) &&
		   Health > 0 && ((MaxHealth > Health && D->RestoreHealth > 0) || (MaxMana > Mana && D->RestoreMana > 0));
}
EProject_JItemUseResult UProject_JInventoryComponent::SubmitPersistentUse(FGuid RequestId, FGuid InstanceId)
{
	FProject_JItemInstanceData Item;
	FindItemInstance(InstanceId, Item);
	TWeakObjectPtr<UProject_JConsumableDefinition> Definition(Cast<UProject_JConsumableDefinition>(Item.ItemDef));
	const auto *I = Cast<IAbilitySystemInterface>(GetOwner());
	auto *ASC = I ? I->GetAbilitySystemComponent() : nullptr;
	TWeakObjectPtr<UProject_JInventoryComponent> WeakThis(this);
	TWeakObjectPtr<UAbilitySystemComponent> WeakASC(ASC);
	TWeakObjectPtr<AActor> Avatar(ASC ? ASC->GetAvatarActor() : nullptr),
		ASCOwner(ASC ? ASC->GetOwnerActor() : nullptr);
	const bool HadAvatar = Avatar.IsValid(), HadASCOwner = ASCOwner.IsValid();
	const auto *ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	const uint64 ActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
	FProject_JPersistentEdit Edit;
	Edit.Kind = EProject_JPersistentEdit::Use;
	Edit.Request = RequestId;
	Edit.Instance = InstanceId;
	const bool Accepted = SubmitPersistentEdit(Edit, [WeakThis, Definition, WeakASC, Avatar, ASCOwner, HadAvatar,
													  HadASCOwner, ActorInfoRevision, RequestId](bool OK, bool First) {
		auto *Self = WeakThis.Get();
		if (!Self || !Self->CanPublishState())
			return;
		const TStrongObjectPtr<UProject_JInventoryComponent> KeepSelf(Self);
		const TStrongObjectPtr<AActor> KeepOwner(Self->GetOwner());
		if (OK && First)
		{
			auto *A = WeakASC.Get();
			auto *D = Definition.Get();
			const auto *Current = Cast<IAbilitySystemInterface>(Self->GetOwner());
			const auto *CurrentASC = Cast<UProject_JAbilitySystemComponent>(A);
			OK = CurrentASC && CurrentASC->GetActorInfoRevision() == ActorInfoRevision;
			const TStrongObjectPtr<UAbilitySystemComponent> KeepASC(A);
			const TStrongObjectPtr<UProject_JConsumableDefinition> KeepDefinition(D);
			const TStrongObjectPtr<AActor> KeepAvatar(Avatar.Get());
			OK = OK && Self->HasMutationAuthority() && A && !A->IsBeingDestroyed() && D && Current &&
				 Current->GetAbilitySystemComponent() == A && (!HadAvatar || Avatar.IsValid()) &&
				 (!HadASCOwner || ASCOwner.IsValid()) && A->GetAvatarActor() == Avatar.Get() &&
				 A->GetOwnerActor() == ASCOwner.Get() && (!Avatar.IsValid() || !Avatar->IsActorBeingDestroyed()) &&
				 (!ASCOwner.IsValid() || !ASCOwner->IsActorBeingDestroyed());
			if (OK)
			{
				const float H = A->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
				OK = FMath::IsFinite(H) && H > 0 &&
					 FMath::IsFinite(A->GetNumericAttribute(UProject_JAttributeSet::GetManaAttribute())) &&
					 FMath::IsFinite(A->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute())) &&
					 FMath::IsFinite(A->GetNumericAttribute(UProject_JAttributeSet::GetMaxManaAttribute())) &&
					 FMath::IsFinite(D->RestoreHealth) && FMath::IsFinite(D->RestoreMana) && D->RestoreHealth >= 0 &&
					 D->RestoreMana >= 0 && FMath::IsFinite(D->CooldownSeconds) && D->CooldownSeconds >= .1f &&
					 D->CooldownSeconds <= 3600;
				if (OK)
				{
					auto *Effect = NewObject<UGameplayEffect>();
					Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
					const TStrongObjectPtr<UGameplayEffect> KeepEffect(Effect);
					auto Add = [Effect](FGameplayAttribute Attribute, float Amount) {
						if (Amount > 0)
						{
							auto &M = Effect->Modifiers.AddDefaulted_GetRef();
							M.Attribute = Attribute;
							M.ModifierOp = EGameplayModOp::Additive;
							M.ModifierMagnitude = FScalableFloat(Amount);
						}
					};
					Add(UProject_JAttributeSet::GetHealthAttribute(),
						FMath::Clamp(A->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute()) - H, 0.f,
									 D->RestoreHealth));
					Add(UProject_JAttributeSet::GetManaAttribute(),
						FMath::Clamp(A->GetNumericAttribute(UProject_JAttributeSet::GetMaxManaAttribute()) -
										 A->GetNumericAttribute(UProject_JAttributeSet::GetManaAttribute()),
									 0.f, D->RestoreMana));
					Self->NextUseTime = Self->GetWorld()->GetTimeSeconds() + D->CooldownSeconds;
					FGameplayEffectSpec Spec(Effect, A->MakeEffectContext(), 1.f);
					A->ApplyGameplayEffectSpecToSelf(Spec);
				}
			}
		}
		if (Self->CanPublishState())
			Self->ClientUseCompleted(
				RequestId, OK ? (First ? EProject_JItemUseResult::Success : EProject_JItemUseResult::Reconciled)
							  : EProject_JItemUseResult::Unavailable);
	});
	return Accepted ? EProject_JItemUseResult::Pending : EProject_JItemUseResult::Unavailable;
}
