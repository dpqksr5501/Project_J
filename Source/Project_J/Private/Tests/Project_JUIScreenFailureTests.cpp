#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Framework/Application/SlateApplication.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIScreenFailureLifetimeTest, "ProjectJ.UI.Screen.CreationFailureHasNoSubscriptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIScreenFailureLifetimeTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
	auto* PC = World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
	auto* UI = NewObject<UProject_JPlayerUIComponent>(PC); PC->AddInstanceComponent(UI);
	// This local controller intentionally has no attached ULocalPlayer. UMG must
	// reject creation; the screen owner must not publish partial subscriptions.
	AddExpectedError(TEXT("CreateWidget"), EAutomationExpectedErrorFlags::Contains, 3);
	UI->RegisterComponent();
	TArray<TSharedPtr<IInputProcessor>> Processors;
	TArray<FDelegateHandle> Handles;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		if (Attempt) UI->RefreshSources();
		// Retain failed baseline registrations so the test itself can clean them up.
		Processors.Add(UI->CursorInputProcessor); Handles.Add(UI->ActivationHandle);
		TestNull(TEXT("Failed screen owns no inventory projection"), UI->GetInventoryModel());
		TestFalse(TEXT("Failed screen registers no cursor input"), UI->CursorInputProcessor.IsValid());
		TestFalse(TEXT("Failed screen subscribes no activation callback"), UI->ActivationHandle.IsValid());
	}
	UI->DestroyComponent();
	if (FSlateApplication::IsInitialized())
	{
		for (const auto& Processor : Processors) if (Processor) FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
		for (const auto& Handle : Handles) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(Handle);
	}
	World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}
#endif
