#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	struct FActionFixture
	{
		UWorld* World;
		AProject_JNPCCharacter* NPC;
		AAIController* AI;
		UProject_JTargetScoringComponent* Scoring;
		UProject_JNPCActionComponent* Actions;
		UProject_JNPCDecisionSubsystem* Decisions;
		UProject_JNPCPathSubsystem* Paths;
		FActionFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, P);
			NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
			NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
			AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
			Scoring = NewObject<UProject_JTargetScoringComponent>(NPC);
			NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
			Actions = NewObject<UProject_JNPCActionComponent>(NPC);
			NPC->AddInstanceComponent(Actions); Actions->RegisterComponent();
			Decisions = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
			Paths = World->GetSubsystem<UProject_JNPCPathSubsystem>();
		}
		~FActionFixture()
		{
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		}
		AActor* Target(double X, int32 Team = 2)
		{
			auto* Actor = World->SpawnActor<AActor>();
			auto* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(FVector(X, 0, 0));
			Decisions->RegisterTarget(Actor, Team); return Actor;
		}
		void ActionTick() { Actions->TickComponent(0.1f, LEVELTICK_All, nullptr); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionLifecycleTest, "ProjectJ.NPCAction.IntentLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionLifecycleTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	TestFalse(TEXT("Disabled by default"), F.Actions->IsComponentTickEnabled());
	TestFalse(TEXT("Manual scoring cannot authorize actions"), F.Actions->StartActions(F.Scoring, {}));
	TestTrue(TEXT("Explicit registration"), F.Scoring->StartBatchedNPCDecisions(1));
	TestTrue(TEXT("Opt-in movement-only start"), F.Actions->StartActions(F.Scoring, {}));
	TestTrue(TEXT("Duplicate start is idempotent"), F.Actions->StartActions(F.Scoring, {}));
	auto* A = F.Target(700); auto* B = F.Target(1200);
	F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.ActionTick();
	TestEqual(TEXT("First intent requests path"), F.Actions->GetActionState(), EProjectJNPCActionState::AwaitingPath);
	TestEqual(TEXT("One queued path"), F.Paths->GetRequestCount(), 1);
	F.Scoring->OnQueryCompleted.Broadcast(A, 1); F.ActionTick();
	TestEqual(TEXT("Same target does not duplicate path"), F.Paths->GetStats().Accepted, uint64(1));
	F.Scoring->OnQueryCompleted.Broadcast(B, 1);
	TestEqual(TEXT("Target switch cancels old queued path"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("New target published"), F.Actions->GetIntentTarget(), B);
	F.Scoring->InvalidateQueryContext();
	TestNull(TEXT("Equipment/skill invalidation clears action intent"), F.Actions->GetIntentTarget());
	F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.Decisions->RegisterTarget(A, 1); F.ActionTick();
	TestNull(TEXT("Team changed after selection cannot be pursued"), F.Actions->GetIntentTarget());
	F.Decisions->RegisterTarget(A, 2); F.Scoring->OnQueryCompleted.Broadcast(A, 1);
	F.AI->UnPossess(); F.ActionTick();
	TestEqual(TEXT("Possession change stops action consumer"), F.Actions->GetActionState(), EProjectJNPCActionState::Disabled);
	TestEqual(TEXT("No remaining queued path after stop"), F.Paths->GetRequestCount(), 0);
	TestTrue(TEXT("Stopping consumer preserves scoring registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCActionContextTest, "ProjectJ.NPCAction.ContextSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCActionContextTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	F.Scoring->StartBatchedNPCDecisions(1);
	F.Target(700);
	int32 Invalidations = 0;
	const auto Handle = F.Scoring->OnContextInvalidated.AddLambda([&Invalidations]() { ++Invalidations; });
	F.Decisions->Tick(0);
	TestEqual(TEXT("Routine snapshot preparation preserves consumer intent"), Invalidations, 0);
	F.Scoring->InvalidateQueryContext();
	TestEqual(TEXT("Semantic context invalidation notifies consumers"), Invalidations, 1);
	F.Scoring->OnContextInvalidated.Remove(Handle);
	F.Scoring->StopBatchedNPCDecisions();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPathAdmissionTest, "ProjectJ.NPCAction.PathAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPathAdmissionTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	int32 Callbacks = 0;
	TArray<TStrongObjectPtr<USceneComponent>> Owners;
	TArray<uint64> Tokens;
	for (int32 I = 0; I < UProject_JNPCPathSubsystem::MaxRequests; ++I)
	{
		Owners.Emplace(NewObject<USceneComponent>(F.NPC));
		Tokens.Add(F.Paths->Submit(Owners.Last().Get(), F.NPC, FVector(1000, 0, 0), 1,
			[&Callbacks](const auto&) { ++Callbacks; }));
		TestTrue(TEXT("Capacity entry accepted"), Tokens.Last() != 0);
	}
	TestEqual(TEXT("Overflow rejected"), F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [](const auto&) {}), uint64(0));
	for (const uint64 Token : Tokens) { F.Paths->Cancel(Token); F.Paths->Cancel(Token); }
	TestEqual(TEXT("Queued cancellation releases all slots"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("Cancellation has no callback"), Callbacks, 0);
	const auto Token = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [&Callbacks](const auto&) { ++Callbacks; });
	TestTrue(TEXT("Capacity reusable"), Token != 0);
	TestEqual(TEXT("One outstanding request per owner"), F.Paths->Submit(F.Actions, F.NPC, FVector(1200, 0, 0), 2, [](const auto&) {}), uint64(0));
	F.AI->UnPossess(); F.Paths->Tick(0);
	TestEqual(TEXT("Possession loss cancels before dispatch"), F.Paths->GetRequestCount(), 0);
	TestEqual(TEXT("Possession cancellation has no callback"), Callbacks, 0);
	F.Paths->OnWorldEndPlay(*F.World);
	F.AI->Possess(F.NPC);
	TestEqual(TEXT("Stopped world rejects submissions"), F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [](const auto&) {}), uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPathLateCompletionTest, "ProjectJ.NPCAction.LatePathCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPathLateCompletionTest::RunTest(const FString& Parameters)
{
	FActionFixture F;
	int32 Callbacks = 0;
	const uint64 Cancelled = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 1, [&Callbacks](const auto&) { ++Callbacks; });
	F.Paths->SimulateDispatchForTest(Cancelled, false);
	F.Paths->Cancel(Cancelled);
	TestEqual(TEXT("Dispatched cancellation retains slot"), F.Paths->GetRequestCount(), 1);
	TestEqual(TEXT("Owner cannot replace an unfinished cancelled job"), F.Paths->Submit(F.Actions, F.NPC, FVector(1200, 0, 0), 2, [](const auto&) {}), uint64(0));
	F.Paths->Complete(Cancelled, 1, ENavigationQueryResult::Fail, nullptr); F.Paths->Tick(0);
	TestEqual(TEXT("Late cancellation completion has no callback"), Callbacks, 0);
	TestEqual(TEXT("Engine completion releases slot"), F.Paths->GetRequestCount(), 0);
	EProjectJNPCPathStatus DeliveredStatus = EProjectJNPCPathStatus::Success;
	const uint64 Expired = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 3,
		[&](const auto& C) { ++Callbacks; DeliveredStatus = C.Status; });
	F.Paths->SimulateDispatchForTest(Expired, true); F.Paths->Tick(0);
	TestEqual(TEXT("Deadline delivered once"), Callbacks, 1);
	TestEqual(TEXT("Deadline reports expiry"), DeliveredStatus, EProjectJNPCPathStatus::Expired);
	TestEqual(TEXT("Timeout retains physical in-flight slot"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Cancelled, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("Old token does not release new request"), F.Paths->GetRequestCount(), 1);
	F.Paths->Complete(Expired, 1, ENavigationQueryResult::Fail, nullptr); F.Paths->Tick(0);
	TestEqual(TEXT("Expired job cannot deliver again"), Callbacks, 1);
	TestEqual(TEXT("Late expired completion releases slot"), F.Paths->GetRequestCount(), 0);
	const uint64 Teardown = F.Paths->Submit(F.Actions, F.NPC, FVector(1000, 0, 0), 4, [&Callbacks](const auto&) { ++Callbacks; });
	F.Paths->SimulateDispatchForTest(Teardown, false); F.Paths->OnWorldEndPlay(*F.World);
	F.Paths->Complete(Teardown, 1, ENavigationQueryResult::Fail, nullptr);
	TestEqual(TEXT("World end prevents late delivery"), Callbacks, 1);
	return true;
}
#endif
