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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAbilityEndGrantRetirementTest, "ProjectJ.Internal.GAS.EndGrantRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAbilityEndGrantRetirementTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    AActor* Owner = World->SpawnActor<AActor>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    for (const bool Cancel : {false, true})
    {
        const auto Grant = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JEndedAbilityFixture::StaticClass(), 1));
        if (!TestTrue(TEXT("Actual GAS activation succeeds"), ASC->TryActivateAbility(Grant))) return false;
        auto* Spec = ASC->FindAbilitySpecFromHandle(Grant);
        auto* Ability = Spec ? Cast<UProject_JEndedAbilityFixture>(Spec->GetPrimaryInstance()) : nullptr;
        if (!TestNotNull(TEXT("Real active ability instance exists"), Ability)) return false;
        int32 EndCalls = 0;
        FGameplayAbilitySpecHandle Replacement;
        const auto Listener = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
        {
            if (Data.AbilitySpecHandle != Grant) return;
            ++EndCalls;
            TestEqual(TEXT("Natural/cancel meaning survives the callback"), Data.bWasCancelled, Cancel);
            ASC->ClearAbility(Grant);
            const auto* Pending = ASC->FindAbilitySpecFromHandle(Grant);
            TestTrue(TEXT("Retired grant remains pending through GAS end postprocessing"), Pending && Pending->PendingRemove && !Pending->IsActive());
            TestNull(TEXT("Pending removal is excluded from live-spec lookup"), ASC->FindAbilitySpecFromHandle(Grant, EConsiderPending::None));
            Replacement = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JEndedAbilityFixture::StaticClass(), 1));
        });
        if (Cancel) ASC->CancelAbilityHandle(Grant); else Ability->CompleteForTest();
        ASC->OnAbilityEnded.Remove(Listener);
        TestEqual(TEXT("The original ability ends exactly once"), EndCalls, 1);
        TestNull(TEXT("Retired grant is removed before returning to the caller"), ASC->FindAbilitySpecFromHandle(Grant));
        TestNotNull(TEXT("A reentrant new grant survives the old retirement"), ASC->FindAbilitySpecFromHandle(Replacement));
        TestTrue(TEXT("The new grant can activate after GAS settles"), ASC->TryActivateAbility(Replacement));
        ASC->CancelAbilityHandle(Replacement); ASC->ClearAbility(Replacement);
        TestEqual(TEXT("No grant or pending mutation remains"), ASC->GetActivatableAbilities().Num(), 0);
    }
    return true;
}
#endif
