#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/Project_JInventoryViewModel.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JConsumableDefinition.h"

namespace ProjectJUITests
{
struct FWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AActor* Owner;
	UProject_JInventoryComponent* Inventory;
	UProject_JEquipmentManagerComponent* Equipment;
	FWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Owner = World->SpawnActor<AActor>();
		Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
		Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner); Equipment->RegisterComponent();
	}
	~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	FProject_JItemInstanceData AddHelmet()
	{
		auto* Definition = NewObject<UProject_JEquipmentItemDefinition>(Owner);
		Definition->ItemId = FName(*FGuid::NewGuid().ToString());
		Definition->EquipmentSlot = EProject_JEquipmentSlot::Head;
		return Inventory->AddItemDefinition(Definition);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIDragIdentityTest, "ProjectJ.UI.Inventory.StaleDragAndSlotValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIDragIdentityTest::RunTest(const FString&)
{
	ProjectJUITests::FWorld W;
	const auto First = W.AddHelmet(); const auto Second = W.AddHelmet();
	auto* Model = NewObject<UProject_JInventoryViewModel>(); Model->Bind(W.Inventory, W.Equipment);
	auto* Row = Model->InventoryEntries.FindByPredicate([&](const auto& Entry) { return Entry->Item.InstanceId == First.InstanceId; })->Get();
	TestTrue(TEXT("Owned head item accepts its slot"), Model->CanDrop(Row, EProject_JEquipmentSlot::Head, false));
	TestFalse(TEXT("Incompatible drop rejected"), Model->CanDrop(Row, EProject_JEquipmentSlot::Mount, false));
	TestTrue(TEXT("Request uses server owned instance"), Model->Equip(First.InstanceId, EProject_JEquipmentSlot::Head));
	TestFalse(TEXT("Standalone response clears pending synchronously"), Model->bPending);
	FProject_JItemInstanceData Equipped;
	TestTrue(TEXT("Authority equipped first item"), W.Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Head, Equipped));
	TestEqual(TEXT("Equipped identity"), Equipped.InstanceId, First.InstanceId);
	auto* EquipmentRow = Model->EquipmentEntries.FindByPredicate([](const auto& Entry) { return Entry->Slot == EProject_JEquipmentSlot::Head; })->Get();
	auto* Drag = DuplicateObject<UProject_JInventoryEntry>(EquipmentRow, Model);
	TestTrue(TEXT("Second item replaces slot"), Model->Equip(Second.InstanceId, EProject_JEquipmentSlot::Head));
	TestFalse(TEXT("Frozen drag cannot remove replacement"), Model->CanDrop(Drag, EProject_JEquipmentSlot::None, true));
	Model->Unequip(First.InstanceId, EProject_JEquipmentSlot::Head);
	W.Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Head, Equipped);
	TestEqual(TEXT("Server rejects stale identity even bypassing local validation"), Equipped.InstanceId, Second.InstanceId);
	Model->Unequip(Second.InstanceId, EProject_JEquipmentSlot::Head);
	TestFalse(TEXT("Correct identity unequips"), W.Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Head, Equipped));
	FProject_JItemInstanceData Returned; W.Inventory->FindItemInstance(Second.InstanceId, Returned);
	TestFalse(TEXT("Unequip releases inventory lock"), Returned.bIsLocked);
	Model->Unbind(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIProjectionLifetimeTest, "ProjectJ.UI.Inventory.ProjectionLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIProjectionLifetimeTest::RunTest(const FString&)
{
	ProjectJUITests::FWorld W; const auto Item = W.AddHelmet();
	auto* Model = NewObject<UProject_JInventoryViewModel>(); Model->Bind(W.Inventory, W.Equipment);
	auto* Row = Model->InventoryEntries[0].Get();
	Model->Refresh(); TestEqual(TEXT("Refresh preserves row identity"), Model->InventoryEntries[0].Get(), Row);
	auto* OtherModel = NewObject<UProject_JInventoryViewModel>(); OtherModel->Bind(W.Inventory, W.Equipment);
	TestFalse(TEXT("Cross model drag cannot cross player screen"), OtherModel->CanDrop(Row, EProject_JEquipmentSlot::Head, false));
	Model->Unbind();
	W.Inventory->SetItemInstanceLocked(Item.InstanceId, true, false);
	W.World->GetTimerManager().Tick(0.1f);
	TestTrue(TEXT("Closed menu releases rows and ignores queued deltas"), Model->InventoryEntries.IsEmpty());
	TestFalse(TEXT("Closed menu clears pending"), Model->bPending);
	Model->Bind(W.Inventory, W.Equipment);
	TestTrue(TEXT("Reopen reads final lock state"), Model->InventoryEntries[0]->Item.bIsLocked);
	Model->Unbind(); OtherModel->Unbind(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIRequestLifetimeTest, "ProjectJ.UI.Inventory.DelayedReplyTimeoutAndRebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIRequestLifetimeTest::RunTest(const FString &)
{
	TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
	ProjectJUITests::FWorld Old; ProjectJUITests::FWorld New;
	auto *Model = NewObject<UProject_JInventoryViewModel>(); Model->Bind(Old.Inventory, Old.Equipment);
	const FGuid OldRequest = FGuid::NewGuid();
	Model->PendingRequest = OldRequest; Model->bPending = true;
	Model->ArmRequestTimer(Old.World, UProject_JInventoryViewModel::ERequestKind::Bag);
	Old.Inventory->OnUseCompleted.Broadcast(OldRequest, EProject_JItemUseResult::Success);
	TestTrue(TEXT("Wrong operation reply cannot unlock pending bag request"), Model->bPending);
	Old.Inventory->OnBagCompleted.Broadcast(FGuid::NewGuid(), EProject_JBagResult::Success);
	TestTrue(TEXT("Unrelated/out-of-order reply ignored"), Model->bPending);
	Old.World->GetTimerManager().Tick(.1f);
	++GFrameCounter; // TimerManager intentionally ignores a second tick in the same engine frame.
	Old.World->GetTimerManager().Tick(5.1f);
	TestFalse(TEXT("Lost reply releases pending after bounded timeout"), Model->bPending);
	const FText TimeoutStatus = Model->Status;
	Old.Inventory->OnBagCompleted.Broadcast(OldRequest, EProject_JBagResult::Success);
	TestTrue(TEXT("Late ack cannot replace timeout status"), Model->Status.EqualTo(TimeoutStatus));
	Model->PendingRequest = FGuid::NewGuid(); Model->bPending = true;
	Model->ArmRequestTimer(Old.World, UProject_JInventoryViewModel::ERequestKind::Equipment);
	const FTimerHandle OldTimer = Model->RequestTimer;
	Model->Bind(New.Inventory, New.Equipment);
	TestFalse(TEXT("Character/source switch clears old-world timer"), Old.World->GetTimerManager().TimerExists(OldTimer));
	const FGuid NewRequest = FGuid::NewGuid(); Model->PendingRequest = NewRequest; Model->bPending = true;
	Model->ArmRequestTimer(New.World, UProject_JInventoryViewModel::ERequestKind::Bag);
	Old.Inventory->OnBagCompleted.Broadcast(NewRequest, EProject_JBagResult::Success);
	TestTrue(TEXT("Detached source cannot affect current player even with matching ID"), Model->bPending);
	New.Inventory->OnBagCompleted.Broadcast(NewRequest, EProject_JBagResult::Success);
	TestFalse(TEXT("Current reply unlocks correctly"), Model->bPending);
	Model->Unbind(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIItemAggregateTest, "ProjectJ.UI.Inventory.QuickSlotAggregateProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIItemAggregateTest::RunTest(const FString &)
{
	ProjectJUITests::FWorld W;
	auto *Potion = NewObject<UProject_JConsumableDefinition>(W.Owner);
	Potion->ItemId = TEXT("UITestPotion"); Potion->MaxStackCount = 99;
	const auto Locked = W.Inventory->AddItemDefinition(Potion, 10);
	W.Inventory->SetItemInstanceLocked(Locked.InstanceId, true, false);
	const auto Usable = W.Inventory->AddItemDefinition(Potion, 4);
	auto *Model = NewObject<UProject_JInventoryViewModel>(); Model->Bind(W.Inventory, W.Equipment);
	const auto *Summary = Model->FindItemSummary(Potion->ItemId);
	TestTrue(TEXT("Summary exists"), Summary != nullptr);
	if (!Summary) { Model->Unbind(); return false; }
	TestEqual(TEXT("Quantity includes locked owned stacks"), Summary->Quantity, 14);
	TestEqual(TEXT("Quick slot chooses an unlocked instance"), Summary->UsableInstance, Usable.InstanceId);
	Model->SetFilter(TEXT("HiddenBySearch"), 1, true);
	TestTrue(TEXT("Search hides visible rows"), Model->VisibleEntries.IsEmpty());
	TestEqual(TEXT("Search does not change quick-slot ownership projection"), Model->FindItemSummary(Potion->ItemId)->Quantity, 14);
	W.Inventory->SetItemStackCount(Usable.InstanceId, 3);
	W.Inventory->RemoveItemInstance(Usable.InstanceId);
	W.World->GetTimerManager().Tick(.1f);
	Summary = Model->FindItemSummary(Potion->ItemId);
	TestEqual(TEXT("Coalesced delta reads final quantity"), Summary->Quantity, 10);
	TestFalse(TEXT("Removed item cannot remain actionable in cache"), Summary->UsableInstance.IsValid());
	Model->Unbind(); TestNull(TEXT("Unbind releases aggregation"), Model->FindItemSummary(Potion->ItemId));
	return true;
}
#endif
