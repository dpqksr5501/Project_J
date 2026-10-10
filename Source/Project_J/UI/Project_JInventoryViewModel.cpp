#include "UI/Project_JInventoryViewModel.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/World.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DECLARE_CATEGORY_EXTERN(ProjectJUI);

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
	++SourceRevision;
	ClearRequest();
	if (RefreshWorld.IsValid()) RefreshWorld->GetTimerManager().ClearTimer(RefreshTimer);
	RefreshTimer.Invalidate();
	RefreshWorld.Reset();
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
	ItemSummaries.Reset();
	Status = FText::GetEmpty();
}

void UProject_JInventoryViewModel::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void UProject_JInventoryViewModel::Refresh()
{
	CSV_SCOPED_TIMING_STAT(ProjectJUI, InventoryProjection);
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
	ItemSummaries.Reset();
	for (const UProject_JInventoryEntry *Entry : InventoryEntries)
	{
		const auto &Item = Entry->Item;
		if (!Item.IsValid() || Item.ItemDef->ItemId.IsNone()) continue;
		auto &Summary = ItemSummaries.FindOrAdd(Item.ItemDef->ItemId);
		if (!Summary.Definition.IsValid()) Summary.Definition = Item.ItemDef.Get();
		Summary.Quantity = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Summary.Quantity) + Item.StackCount));
		if (!Summary.UsableInstance.IsValid() && !Item.bIsLocked && !Item.bIsEquipped && Cast<UProject_JConsumableDefinition>(Item.ItemDef))
			Summary.UsableInstance = Item.InstanceId;
	}
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
	CSV_CUSTOM_STAT(ProjectJUI, InventoryRows, InventoryEntries.Num(), ECsvCustomStatOp::Set);
	OnChanged.Broadcast();
}

void UProject_JInventoryViewModel::QueueRefresh()
{
	// FastArray PreReplicatedRemove runs before the old array element is erased.
	// Coalesce a delta batch and read the completed authoritative projection next tick.
	UWorld *World = Inventory.IsValid() ? Inventory->GetWorld() : Equipment.IsValid() ? Equipment->GetWorld() : nullptr;
	if (World && !RefreshTimer.IsValid())
	{
		RefreshWorld = World;
		const uint32 Revision = SourceRevision;
		RefreshTimer = World->GetTimerManager().SetTimerForNextTick(
			[WeakThis = TWeakObjectPtr<UProject_JInventoryViewModel>(this), Revision]()
			{
				if (auto *Self = WeakThis.Get())
				{
					if (Self->SourceRevision != Revision) return;
					Self->RefreshTimer.Invalidate();
					Self->Refresh();
				}
			});
	}
}
void UProject_JInventoryViewModel::ClearRequest()
{
	if (RequestWorld.IsValid()) RequestWorld->GetTimerManager().ClearTimer(RequestTimer);
	RequestWorld.Reset(); RequestTimer.Invalidate();
	bPending = false; PendingRequest.Invalidate(); PendingKind = ERequestKind::None;
}
void UProject_JInventoryViewModel::ArmRequestTimer(UWorld *World, ERequestKind Kind)
{
	PendingKind = Kind;
	RequestWorld = World;
	const FGuid Id = PendingRequest;
	const uint32 Revision = SourceRevision;
	if (World) World->GetTimerManager().SetTimer(RequestTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Id, Revision]()
		{
			if (bPending && PendingRequest == Id && SourceRevision == Revision) RequestTimedOut();
		}), 5.f, false);
}
bool UProject_JInventoryViewModel::UseByItemId(FName ItemId)
{
	const auto *Summary = FindItemSummary(ItemId);
	return Summary && Summary->UsableInstance.IsValid() && Use(Summary->UsableInstance);
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
	ArmRequestTimer(Equipment->GetWorld(), ERequestKind::Equipment);
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
	if (!bPending || PendingKind != ERequestKind::Equipment || Id != PendingRequest)
		return;
	ClearRequest();
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
	if (!bPending) return;
	ClearRequest();
	Status = NSLOCTEXT("ProjectJUI", "Timeout", "응답이 지연됩니다. 현재 서버 상태를 확인한 후 다시 시도하세요.");
	Refresh();
}

void UProject_JInventoryViewModel::RebuildVisible()
{
	CSV_SCOPED_TIMING_STAT(ProjectJUI, InventoryFilter);
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
	const auto SubmittedInventory = Inventory;
	ArmRequestTimer(Inventory->GetWorld(), ERequestKind::Use);
	OnChanged.Broadcast();
	if (Inventory == SubmittedInventory && Inventory.IsValid() && bPending && PendingRequest == Request)
		Inventory->RequestUseItem(Request, InstanceId);
	return true;
}
void UProject_JInventoryViewModel::OnUseCompleted(FGuid Id, EProject_JItemUseResult Result)
{
	if (!bPending || PendingKind != ERequestKind::Use || Id != PendingRequest)
		return;
	ClearRequest();
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
FProject_JEquipmentComparison UProject_JInventoryViewModel::BuildEquipmentComparison(const UProject_JInventoryEntry *Entry) const
{
	const auto *Candidate = Entry ? Cast<UProject_JEquipmentItemDefinition>(Entry->Item.ItemDef) : nullptr;
	FProject_JItemInstanceData Current;
	const UProject_JEquipmentItemDefinition *Equipped = nullptr;
	if (Candidate && Entry->Model == this && !Entry->bEquipmentEntry && Equipment.IsValid() &&
		Equipment->GetEquippedItemInstance(Candidate->EquipmentSlot, Current))
		Equipped = Cast<UProject_JEquipmentItemDefinition>(Current.ItemDef);
	auto Result = FProject_JEquipmentComparison::Build(Candidate, Equipped);
	if (!Entry || Entry->Model != this || !Candidate || (!Entry->bEquipmentEntry && !Equipment.IsValid()))
	{
		Result.bCanCompare = false;
		Result.Explanation = NSLOCTEXT("ProjectJUI", "CompareUnavailable", "현재 장비 정보를 확인할 수 없습니다.");
	}
	return Result;
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
		const auto Comparison = BuildEquipmentComparison(Entry);
		for (const auto &Row : Comparison.Rows)
		{
			if (Comparison.bCanCompare)
				Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "AdditiveStatCompare", "{0}: {1} (고정 보너스 차이 {2})"),
					FProject_JEquipmentComparison::StatName(Row.Stat), FText::AsNumber(Row.Candidate), FText::AsNumber(Row.Difference)));
			else if (Row.Candidate != 0 && Comparison.CandidateMode != EProject_JEquipmentBonusPreview::GameplayEffects)
				Lines.Add(FText::Format(Comparison.CandidateMode == EProject_JEquipmentBonusPreview::ConditionalFallback
					? NSLOCTEXT("ProjectJUI", "FallbackStat", "효과 미적용 시 대체 {0}: {1}")
					: NSLOCTEXT("ProjectJUI", "AuthoredStat", "고정 보너스 {0}: {1}"),
					FProject_JEquipmentComparison::StatName(Row.Stat), FText::AsNumber(Row.Candidate)));
		}
		Lines.Add(Comparison.Explanation);
		if (!Entry->bEquipmentEntry && Equipment.IsValid() &&
			Equipment->GetEquippedItemInstance(EquipmentDef->EquipmentSlot, Current) &&
			Current.InstanceId != Entry->Item.InstanceId)
			Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "CompareTooltip", "현재 장비: {0} (아이템 레벨 {1})"),
									Current.ItemDef->ItemName, FText::AsNumber(Current.ItemLevel)));
	}
	if (Entry->Item.bIsLocked)
		Lines.Add(NSLOCTEXT("ProjectJUI", "LockedTooltip", "잠김 · 장착·사용·이동 불가"));
	if (Entry->Item.bIsEquipped) Lines.Add(NSLOCTEXT("ProjectJUI", "EquippedTooltip", "현재 장착 중"));
	if (bPending) Lines.Add(NSLOCTEXT("ProjectJUI", "PendingTooltip", "서버 확인 중 · 응답 후 조작 가능"));
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
	ArmRequestTimer(Inventory->GetWorld(), ERequestKind::Bag);
	OnChanged.Broadcast();
	if (Inventory == SubmittedInventory && Inventory.IsValid() && bPending && Id == PendingRequest)
		Inventory->RequestBagChange(Id, Request);
	return true;
}
void UProject_JInventoryViewModel::OnBagCompleted(FGuid Id, EProject_JBagResult Result)
{
	if (!bPending || PendingKind != ERequestKind::Bag || Id != PendingRequest) return;
	ClearRequest();
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
