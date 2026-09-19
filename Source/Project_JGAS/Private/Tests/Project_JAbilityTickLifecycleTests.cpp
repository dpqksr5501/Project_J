#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JTickingTaskFixture.h"
#include "Project_JAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAbilityTickLifecycleTest, "ProjectJ.Internal.GAS.TaskTickLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAbilityTickLifecycleTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	AActor* Owner = World->SpawnActor<AActor>();
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Owner, Owner);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	const auto Step = [&]() { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.0f / 60); };
	Step();
	TestFalse(TEXT("GAS sleeps after its idle policy pass"), ASC->IsComponentTickEnabled());
	auto* Task = NewObject<UProject_JTickingTaskFixture>(ASC);
	Task->InitializeForTest(*ASC);
	Task->ReadyForActivation();
	TestTrue(TEXT("Activating a ticking task wakes the registered ASC"), ASC->IsComponentTickEnabled());
	Step();
	Step();
	TestTrue(TEXT("The engine scheduler actually services the task"), Task->TickCount > 0);
	const int32 CompletedTicks = Task->TickCount;
	Task->EndTask();
	Step();
	TestEqual(TEXT("Ended tasks receive no more updates"), Task->TickCount, CompletedTicks);
	TestFalse(TEXT("Ending the final task returns GAS to sleep"), ASC->IsComponentTickEnabled());
	return true;
}
#endif
