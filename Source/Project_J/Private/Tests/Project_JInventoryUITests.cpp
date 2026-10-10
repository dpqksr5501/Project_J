#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/Project_JInventoryViewModel.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"

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
#endif
