#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIModelOwnershipTest,
	"ProjectJ.Continuation.UIModelOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIModelOwnershipTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Controller = World->SpawnActor<APlayerController>();
	auto* First = NewObject<UProject_JPlayerUIComponent>(Controller);
	auto* Second = NewObject<UProject_JPlayerUIComponent>(Controller);
	Controller->AddInstanceComponent(First); Controller->AddInstanceComponent(Second);
	auto* A = NewObject<UProject_JInventoryViewModel>(First);
	auto* B = NewObject<UProject_JInventoryViewModel>(Second);
	auto* Property = FindFProperty<FObjectPropertyBase>(UProject_JPlayerUIComponent::StaticClass(), TEXT("InventoryModel"));
	if (!TestNotNull(TEXT("Reflected model binding"), Property)) { return false; }
	Property->SetObjectPropertyValue_InContainer(First, A);
	Property->SetObjectPropertyValue_InContainer(Second, B);
	auto* HUD = NewObject<UProject_JPlayerHUDWidget>(Controller);
	int32 OtherSubscriberCalls = 0;
	const auto Other = A->OnSplitRequested.AddLambda([&](UProject_JInventoryEntry*) { ++OtherSubscriberCalls; });
	ON_SCOPE_EXIT { A->OnSplitRequested.Remove(Other); };
	HUD->InitializeScreen(First);
	TestTrue(TEXT("First model owns its screen subscription"), A->OnSplitRequested.IsBoundToObject(HUD));
	HUD->InitializeScreen(First);
	HUD->InitializeScreen(Second);
	TestFalse(TEXT("Rebinding retires the previous model subscription"), A->OnSplitRequested.IsBoundToObject(HUD));
	TestTrue(TEXT("Current model owns the screen subscription"), B->OnSplitRequested.IsBoundToObject(HUD));
	A->OnSplitRequested.Broadcast(nullptr);
	TestEqual(TEXT("Unrelated subscribers are preserved"), OtherSubscriberCalls, 1);
	TestTrue(TEXT("Retired model cannot publish guidance into the new model"), B->Status.IsEmpty());
	HUD->InitializeScreen(nullptr);
	TestFalse(TEXT("Clearing the screen binding returns its subscription"), B->OnSplitRequested.IsBoundToObject(HUD));
	B->Status = FText::FromString(TEXT("server request pending")); B->bPending = true;
	B->SetLocalStatus(FText::GetEmpty());
	TestEqual(TEXT("Local cancel cannot erase an in-flight server status"), B->Status.ToString(), FString(TEXT("server request pending")));
	B->bPending = false;
	B->SetLocalStatus(FText::FromString(TEXT("choose merge target")));
	TestEqual(TEXT("Idle model accepts local guidance"), B->Status.ToString(), FString(TEXT("choose merge target")));
	B->SetLocalStatus(FText::GetEmpty());
	TestTrue(TEXT("Idle guidance can be cleared"), B->Status.IsEmpty());
	return true;
}
#endif
