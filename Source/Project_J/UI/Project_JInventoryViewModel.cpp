#include "UI/Project_JInventoryViewModel.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/World.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "TimerManager.h"

void UProject_JInventoryViewModel::Bind(UProject_JInventoryComponent *InInventory,
										UProject_JEquipmentManagerComponent *InEquipment)
{
	if (Inventory == InInventory && Equipment == InEquipment)
		return;
	Unbind();
	Inventory = InInventory;
	Equipment = InEquipment;
	if (Inventory.IsValid())
	{
		Inventory->OnItemAdded.AddDynamic(this, &ThisClass::OnItemChanged);
		Inventory->OnUseCompleted.AddDynamic(this, &ThisClass::OnUseCompleted);
		Inventory->OnBagCompleted.AddDynamic(this, &ThisClass::OnBagCompleted);
		Inventory->OnItemChanged.AddDynamic(this, &ThisClass::OnItemChanged);
		Inventory->OnItemRemoved.AddDynamic(this, &ThisClass::OnItemChanged);
	}
	if (Equipment.IsValid())
	{
		Equipment->OnEquipmentEquipped.AddDynamic(this, &ThisClass::OnEquipmentChanged);
		Equipment->OnEquipmentUnequipped.AddDynamic(this, &ThisClass::OnEquipmentChanged);
		Equipment->OnRequestCompleted.AddDynamic(this, &ThisClass::OnCompleted);
	}
	Refresh();
}

void UProject_JInventoryViewModel::Unbind()
{
	if (Inventory.IsValid())
	{
		if (auto *World = Inventory->GetWorld())
		{
			World->GetTimerManager().ClearTimer(RequestTimer);
			World->GetTimerManager().ClearTimer(RefreshTimer);
		}
		Inventory->OnUseCompleted.RemoveAll(this);
		Inventory->OnBagCompleted.RemoveAll(this);
		Inventory->OnItemAdded.RemoveAll(this);
		Inventory->OnItemChanged.RemoveAll(this);
		Inventory->OnItemRemoved.RemoveAll(this);
	}
	if (Equipment.IsValid())
	{
		if (auto *World = Equipment->GetWorld())
		{
			World->GetTimerManager().ClearTimer(RequestTimer);
			World->GetTimerManager().ClearTimer(RefreshTimer);
		}
		Equipment->OnEquipmentEquipped.RemoveAll(this);
		Equipment->OnEquipmentUnequipped.RemoveAll(this);
		Equipment->OnRequestCompleted.RemoveAll(this);
	}
	Inventory.Reset();
	Equipment.Reset();
	PendingRequest.Invalidate();
	bPending = false;
	VisibleEntries.Reset();
	InventoryEntries.Reset();
	EquipmentEntries.Reset();
	Status = FText::GetEmpty();
}

void UProject_JInventoryViewModel::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void UProject_JInventoryViewModel::Refresh()
{
	TMap<FGuid, UProject_JInventoryEntry *> Previous;
	for (UProject_JInventoryEntry *Entry : InventoryEntries)
		if (Entry)
			Previous.Add(Entry->Item.InstanceId, Entry);
	InventoryEntries.Reset();
	if (Inventory.IsValid())
		for (const auto &Item : Inventory->GetItemInstances())
		{
			auto *Entry = Previous.FindRef(Item.InstanceId);
			if (!Entry)
				Entry = NewObject<UProject_JInventoryEntry>(this);
			Entry->Item = Item;
			Entry->Model = this;
			const auto *Definition = Cast<UProject_JEquipmentItemDefinition>(Item.ItemDef);
			Entry->Slot = Definition ? Definition->EquipmentSlot : EProject_JEquipmentSlot::None;
			InventoryEntries.Add(Entry);
		}
	InventoryEntries.Sort([](const UProject_JInventoryEntry &A, const UProject_JInventoryEntry &B)
		{ return A.Item.BagOrder == B.Item.BagOrder ? A.Item.InstanceId < B.Item.InstanceId : A.Item.BagOrder < B.Item.BagOrder; });
	if (EquipmentEntries.IsEmpty())
		for (int32 Slot = 1; Slot <= static_cast<int32>(EProject_JEquipmentSlot::Mount); ++Slot)
		{
			auto *Entry = NewObject<UProject_JInventoryEntry>(this);
			Entry->Model = this;
			Entry->bEquipmentEntry = true;
			Entry->Slot = static_cast<EProject_JEquipmentSlot>(Slot);
			EquipmentEntries.Add(Entry);
		}
	for (UProject_JInventoryEntry *Entry : EquipmentEntries)
	{
		Entry->Item = {};
		if (Equipment.IsValid())
			Equipment->GetEquippedItemInstance(Entry->Slot, Entry->Item);
	}
	RebuildVisible();
	OnChanged.Broadcast();
}

void UProject_JInventoryViewModel::QueueRefresh()
{
	// FastArray PreReplicatedRemove runs before the old array element is erased.
	// Coalesce a delta batch and read the completed authoritative projection next tick.
	UWorld *World = Inventory.IsValid() ? Inventory->GetWorld() : Equipment.IsValid() ? Equipment->GetWorld() : nullptr;
	if (World && !RefreshTimer.IsValid())
		RefreshTimer = World->GetTimerManager().SetTimerForNextTick(
			[WeakThis = TWeakObjectPtr<UProject_JInventoryViewModel>(this)]()
			{
				if (auto *Self = WeakThis.Get())
				{
					Self->RefreshTimer.Invalidate();
					Self->Refresh();
				}
			});
}
void UProject_JInventoryViewModel::OnItemChanged(const FProject_JItemInstanceData &)
{
	QueueRefresh();
}
void UProject_JInventoryViewModel::OnEquipmentChanged(EProject_JEquipmentSlot, UProject_JEquipmentItemDefinition *)
{
	QueueRefresh();
}

bool UProject_JInventoryViewModel::CanDrop(const UProject_JInventoryEntry *Entry, EProject_JEquipmentSlot TargetSlot,
										   bool bToInventory) const
{
	if (bPending || !Entry || Entry->Model.Get() != this || !Inventory.IsValid() || !Equipment.IsValid())
		return false;
	FProject_JItemInstanceData Current;
	if (bToInventory)
		return Entry->bEquipmentEntry && Equipment->GetEquippedItemInstance(Entry->Slot, Current) &&
			   Current.InstanceId == Entry->Item.InstanceId;
	if (Entry->bEquipmentEntry || !Inventory->FindItemInstance(Entry->Item.InstanceId, Current) || Current.bIsLocked ||
		Current.bIsEquipped)
		return false;
	const auto *Definition = Cast<UProject_JEquipmentItemDefinition>(Current.ItemDef);
	return Definition && Definition->EquipmentSlot == TargetSlot;
}

bool UProject_JInventoryViewModel::Submit(FGuid InstanceId, EProject_JEquipmentSlot Slot, bool bUnequip)
{
	if (bPending || !Equipment.IsValid() || !InstanceId.IsValid())
		return false;
	PendingRequest = FGuid::NewGuid();
	bPending = true;
	const FGuid SubmittedRequest = PendingRequest;
	const TWeakObjectPtr<UProject_JEquipmentManagerComponent> SubmittedEquipment = Equipment;
	Status = NSLOCTEXT("ProjectJUI", "Pending", "서버 확인 중…");
	if (auto *World = Equipment->GetWorld())
		World->GetTimerManager().SetTimer(RequestTimer, this, &ThisClass::RequestTimedOut, 5.f, false);
	OnChanged.Broadcast();
	// Publish pending before calling: standalone authority can reply synchronously.
	if (Equipment == SubmittedEquipment && Equipment.IsValid() && bPending && PendingRequest == SubmittedRequest)
		Equipment->RequestEquipmentChange(SubmittedRequest, InstanceId, Slot, bUnequip);
	return true;
}

bool UProject_JInventoryViewModel::Equip(FGuid Id, EProject_JEquipmentSlot Slot)
{
	return Submit(Id, Slot, false);
}
bool UProject_JInventoryViewModel::Unequip(FGuid Id, EProject_JEquipmentSlot Slot)
{
	return Submit(Id, Slot, true);
}

void UProject_JInventoryViewModel::OnCompleted(FGuid Id, const FProject_JEquipmentOperationResult &Result)
{
	if (!bPending || Id != PendingRequest)
		return;
	if (Equipment.IsValid() && Equipment->GetWorld())
		Equipment->GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
	bPending = false;
	PendingRequest.Invalidate();
	if (Result.bSucceeded)
		Status = NSLOCTEXT("ProjectJUI", "Confirmed", "장비 변경 완료");
	else
		switch (Result.Failure)
		{
		case EProject_JEquipmentOperationFailure::ItemNotOwned:
			Status = NSLOCTEXT("ProjectJUI", "NotOwned", "가방에 없는 아이템입니다.");
			break;
		case EProject_JEquipmentOperationFailure::ItemLocked:
			Status = NSLOCTEXT("ProjectJUI", "LockedFailure", "잠긴 아이템은 장착할 수 없습니다.");
			break;
		case EProject_JEquipmentOperationFailure::AlreadyEquipped:
			Status = NSLOCTEXT("ProjectJUI", "AlreadyEquipped", "이미 장착한 아이템입니다.");
			break;
		case EProject_JEquipmentOperationFailure::InvalidSlot:
			Status = NSLOCTEXT("ProjectJUI", "WrongSlot", "이 슬롯에 장착할 수 없습니다.");
			break;
		case EProject_JEquipmentOperationFailure::SlotEmpty:
			Status = NSLOCTEXT("ProjectJUI", "SlotEmpty", "이미 비어 있는 장비 슬롯입니다.");
			break;
		case EProject_JEquipmentOperationFailure::RateLimited:
		case EProject_JEquipmentOperationFailure::OperationInProgress:
			Status = NSLOCTEXT("ProjectJUI", "Busy", "잠시 후 다시 시도하세요.");
			break;
		default:
			Status =
				NSLOCTEXT("ProjectJUI", "ChangedOrUnavailable", "아이템 상태가 바뀌었거나 현재 변경할 수 없습니다.");
			break;
		}
	Refresh();
}

void UProject_JInventoryViewModel::RequestTimedOut()
{
	bPending = false;
	PendingRequest.Invalidate();
	Status = NSLOCTEXT("ProjectJUI", "Timeout", "응답이 지연됩니다. 현재 서버 상태를 확인한 후 다시 시도하세요.");
	Refresh();
}

void UProject_JInventoryViewModel::RebuildVisible()
{
	VisibleEntries.Reset();
	for (UProject_JInventoryEntry *Entry : InventoryEntries)
	{
		if (!Entry || !Entry->Item.ItemDef)
			continue;
		const UProject_JItemDefinition *Def = Entry->Item.ItemDef;
		if (FilterKind == 1 && !Cast<UProject_JEquipmentItemDefinition>(Def))
			continue;
		if (FilterKind == 2 && !Cast<UProject_JConsumableDefinition>(Def))
			continue;
		if (!SearchQuery.IsEmpty() && !Def->ItemName.ToString().Contains(SearchQuery, ESearchCase::IgnoreCase) &&
			!Def->ItemId.ToString().Contains(SearchQuery, ESearchCase::IgnoreCase))
			continue;
		VisibleEntries.Add(Entry);
	}
	if (bNameSort)
		VisibleEntries.Sort(
			[](const UProject_JInventoryEntry &A, const UProject_JInventoryEntry &B)
			{
				const int32 Compare = A.Item.ItemDef->ItemName.CompareTo(B.Item.ItemDef->ItemName);
				return Compare == 0 ? A.Item.InstanceId < B.Item.InstanceId : Compare < 0;
			});
}
void UProject_JInventoryViewModel::SetFilter(const FString &Query, int32 Kind, bool bSortByName)
{
	SearchQuery = Query.Left(128).TrimStartAndEnd();
	FilterKind = FMath::Clamp(Kind, 0, 2);
	bNameSort = bSortByName;
	RebuildVisible();
	OnChanged.Broadcast();
}
bool UProject_JInventoryViewModel::Use(FGuid InstanceId)
{
	if (bPending || !Inventory.IsValid() || !InstanceId.IsValid())
		return false;
	PendingRequest = FGuid::NewGuid();
	bPending = true;
	Status = NSLOCTEXT("ProjectJUI", "UsePending", "사용 확인 중…");
	const auto Request = PendingRequest;
	if (auto *World = Inventory->GetWorld())
		World->GetTimerManager().SetTimer(RequestTimer, this, &ThisClass::RequestTimedOut, 5.f, false);
	OnChanged.Broadcast();
	if (Inventory.IsValid() && bPending && PendingRequest == Request)
		Inventory->RequestUseItem(Request, InstanceId);
	return true;
}
void UProject_JInventoryViewModel::OnUseCompleted(FGuid Id, EProject_JItemUseResult Result)
{
	if (!bPending || Id != PendingRequest)
		return;
	if (Inventory.IsValid() && Inventory->GetWorld())
		Inventory->GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
	bPending = false;
	PendingRequest.Invalidate();
	switch (Result)
	{
	case EProject_JItemUseResult::Success:
		Status = NSLOCTEXT("ProjectJUI", "Used", "아이템 사용 완료");
		break;
	case EProject_JItemUseResult::NoEffect:
		Status = NSLOCTEXT("ProjectJUI", "NoRecovery", "회복할 자원이 없습니다.");
		break;
	case EProject_JItemUseResult::Cooldown:
		Status = NSLOCTEXT("ProjectJUI", "UseCooldown", "재사용 대기 중입니다.");
		break;
	case EProject_JItemUseResult::Locked:
		Status = NSLOCTEXT("ProjectJUI", "UseLocked", "잠긴 아이템입니다.");
		break;
	default:
		Status = NSLOCTEXT("ProjectJUI", "UseUnavailable", "현재 사용할 수 없습니다.");
		break;
	}
	Refresh();
}
FText UProject_JInventoryViewModel::BuildTooltip(const UProject_JInventoryEntry *Entry) const
{
	if (!Entry || !Entry->Item.ItemDef)
		return NSLOCTEXT("ProjectJUI", "EmptyEquipmentTooltip", "비어 있는 장비 슬롯 · 호환 장비를 드래그하세요");
	const UProject_JItemDefinition *Def = Entry->Item.ItemDef;
	TArray<FText> Lines{Def->ItemName, Def->ItemDescription};
	Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "ItemDetailsTooltip", "아이템 레벨 {0} · 수량 {1}"),
							FText::AsNumber(Entry->Item.ItemLevel), FText::AsNumber(Entry->Item.StackCount)));
	if (const auto *Consumable = Cast<UProject_JConsumableDefinition>(Def))
		Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "RecoveryTooltip", "체력 +{0} · 마나 +{1} · 대기 {2}초"),
								FText::AsNumber(Consumable->RestoreHealth), FText::AsNumber(Consumable->RestoreMana),
								FText::AsNumber(Consumable->CooldownSeconds)));
	if (const auto *EquipmentDef = Cast<UProject_JEquipmentItemDefinition>(Def))
	{
		Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "EquipmentSlotTooltip", "장비 슬롯: {0}"),
								StaticEnum<EProject_JEquipmentSlot>()->GetDisplayNameTextByValue(
									static_cast<int64>(EquipmentDef->EquipmentSlot))));
		FProject_JItemInstanceData Current;
		const UProject_JEquipmentItemDefinition *CurrentDef = nullptr;
		if (!Entry->bEquipmentEntry && Equipment.IsValid() &&
			Equipment->GetEquippedItemInstance(EquipmentDef->EquipmentSlot, Current))
			CurrentDef = Cast<UProject_JEquipmentItemDefinition>(Current.ItemDef);
		if (EquipmentDef->StatApplicationPolicy != EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const auto Stat = static_cast<EProject_JEquipmentStat>(Index);
				float Candidate = 0, Previous = 0;
				for (const auto &Modifier : EquipmentDef->StatModifiers)
					if (Modifier.Stat == Stat)
						Candidate += Modifier.Value;
				if (CurrentDef &&
					CurrentDef->StatApplicationPolicy != EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly)
					for (const auto &Modifier : CurrentDef->StatModifiers)
						if (Modifier.Stat == Stat)
							Previous += Modifier.Value;
				if (FMath::IsFinite(Candidate) && FMath::IsFinite(Previous) && (Candidate != 0 || Previous != 0))
					Lines.Add(FText::Format(
						NSLOCTEXT("ProjectJUI", "FixedStatCompare", "고정 보너스 {0}: {1} (현재 장비 대비 {2})"),
						StaticEnum<EProject_JEquipmentStat>()->GetDisplayNameTextByValue(Index),
						FText::AsNumber(Candidate), FText::AsNumber(Candidate - Previous)));
			}
		}
		if (!EquipmentDef->EquipmentEffects.IsEmpty())
			Lines.Add(NSLOCTEXT("ProjectJUI", "EffectStatsTooltip", "추가 효과는 장착 조건에 따라 적용됩니다."));
		if (!Entry->bEquipmentEntry && Equipment.IsValid() &&
			Equipment->GetEquippedItemInstance(EquipmentDef->EquipmentSlot, Current) &&
			Current.InstanceId != Entry->Item.InstanceId)
			Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "CompareTooltip", "현재 장비: {0} (아이템 레벨 {1})"),
									Current.ItemDef->ItemName, FText::AsNumber(Current.ItemLevel)));
	}
	if (Entry->Item.bIsLocked)
		Lines.Add(NSLOCTEXT("ProjectJUI", "LockedTooltip", "잠김"));
	Lines.Add(NSLOCTEXT("ProjectJUI", "ItemControlsTooltip",
						"우클릭/더블클릭: 장착·해제·사용\nShift+클릭: 수량 분할\n가방 드래그: 위치 교환 · Ctrl+드래그: 합치기\n장비 슬롯 또는 잠금 해제한 퀵슬롯으로 드래그"));
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

namespace
{
FProject_JBagRequest BagRequest(EProject_JBagOperation Operation, const UProject_JInventoryEntry *Source,
	const UProject_JInventoryEntry *Target = nullptr)
{
	FProject_JBagRequest Request;
	Request.Operation = Operation;
	Request.Source = Source->Item.InstanceId;
	Request.ExpectedSourceCount = Source->Item.StackCount;
	Request.ExpectedSourceOrder = Source->Item.BagOrder;
	if (Target)
	{
		Request.Target = Target->Item.InstanceId;
		Request.ExpectedTargetCount = Target->Item.StackCount;
		Request.ExpectedTargetOrder = Target->Item.BagOrder;
	}
	return Request;
}
}
bool UProject_JInventoryViewModel::Split(const UProject_JInventoryEntry *Entry, int32 Count)
{
	if (!Entry || Entry->Model != this || Entry->bEquipmentEntry || Count <= 0 || Count >= Entry->Item.StackCount)
		return false;
	auto Request = BagRequest(EProject_JBagOperation::Split, Entry);
	Request.Count = Count;
	return SubmitBag(Request);
}
bool UProject_JInventoryViewModel::CanDropInBag(const UProject_JInventoryEntry *Source,
	const UProject_JInventoryEntry *Target, bool bMerge) const
{
	if (bPending || !Inventory.IsValid() || !Source || !Target || Source->Model != this || Target->Model != this ||
		Source->bEquipmentEntry || Target->bEquipmentEntry || Source->Item.InstanceId == Target->Item.InstanceId ||
		!Source->Item.IsValid() || !Target->Item.IsValid() || Source->Item.bIsLocked || Target->Item.bIsLocked ||
		Source->Item.bIsEquipped || Target->Item.bIsEquipped) return false;
	if (!bMerge) return !bNameSort && FilterKind == 0 && SearchQuery.IsEmpty();
	return Source->Item.ItemDef == Target->Item.ItemDef && Source->Item.ItemLevel == Target->Item.ItemLevel &&
		Target->Item.StackCount < Target->Item.ItemDef->MaxStackCount;
}
bool UProject_JInventoryViewModel::DropInBag(const UProject_JInventoryEntry *Source,
	const UProject_JInventoryEntry *Target, bool bMerge)
{
	if (!CanDropInBag(Source, Target, bMerge))
	{
		Status = NSLOCTEXT("ProjectJUI", "BagDropInvalid", "합칠 수 없는 항목입니다. 위치 교환은 검색·분류·정렬을 해제한 전체 가방에서 가능합니다.");
		OnChanged.Broadcast();
		return false;
	}
	auto Request = BagRequest(bMerge ? EProject_JBagOperation::Merge : EProject_JBagOperation::Swap, Source, Target);
	if (bMerge) Request.Count = FMath::Min(Source->Item.StackCount, Target->Item.ItemDef->MaxStackCount - Target->Item.StackCount);
	return SubmitBag(Request);
}
bool UProject_JInventoryViewModel::SubmitBag(const FProject_JBagRequest &Request)
{
	if (bPending || !Inventory.IsValid()) return false;
	PendingRequest = FGuid::NewGuid();
	bPending = true;
	Status = NSLOCTEXT("ProjectJUI", "BagPending", "가방 변경 확인 중…");
	const auto Id = PendingRequest;
	const auto SubmittedInventory = Inventory;
	if (auto *World = Inventory->GetWorld()) World->GetTimerManager().SetTimer(RequestTimer, this, &ThisClass::RequestTimedOut, 5.f, false);
	OnChanged.Broadcast();
	if (Inventory == SubmittedInventory && Inventory.IsValid() && bPending && Id == PendingRequest)
		Inventory->RequestBagChange(Id, Request);
	return true;
}
void UProject_JInventoryViewModel::OnBagCompleted(FGuid Id, EProject_JBagResult Result)
{
	if (!bPending || Id != PendingRequest) return;
	if (Inventory.IsValid() && Inventory->GetWorld()) Inventory->GetWorld()->GetTimerManager().ClearTimer(RequestTimer);
	bPending = false;
	PendingRequest.Invalidate();
	switch (Result)
	{
	case EProject_JBagResult::Success: Status = NSLOCTEXT("ProjectJUI", "BagChanged", "가방 변경 완료"); break;
	case EProject_JBagResult::Locked: Status = NSLOCTEXT("ProjectJUI", "BagLocked", "잠긴 아이템이나 장착한 아이템은 이동할 수 없습니다."); break;
	case EProject_JBagResult::Changed: Status = NSLOCTEXT("ProjectJUI", "BagStale", "아이템 수량이나 위치가 바뀌었습니다. 다시 선택하세요."); break;
	case EProject_JBagResult::Full: Status = NSLOCTEXT("ProjectJUI", "BagFull", "스택 또는 가방 변경 한도에 도달했습니다."); break;
	case EProject_JBagResult::Incompatible: Status = NSLOCTEXT("ProjectJUI", "BagIncompatible", "종류와 아이템 레벨이 같은 스택만 합칠 수 있습니다."); break;
	case EProject_JBagResult::RateLimited: Status = NSLOCTEXT("ProjectJUI", "BagRateLimited", "조작이 너무 빠릅니다. 잠시 후 다시 시도하세요."); break;
	default: Status = NSLOCTEXT("ProjectJUI", "BagUnavailable", "현재 가방을 변경할 수 없습니다."); break;
	}
	Refresh();
}
