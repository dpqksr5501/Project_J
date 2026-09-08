#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Optimization/Project_JNPCDecisionExperiment.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"

namespace
{
	struct FDecisionFixture
	{
		UWorld* World;
		UProject_JNPCDecisionSubsystem* Scheduler;
		UProject_JTargetScoringSubsystem* Service;
		FDecisionFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
			Service = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
		}
		~FDecisionFixture()
		{
			if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		AActor* Target(double X, int32 Team)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			auto* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root); Root->RegisterComponent(); Actor->SetActorLocation(FVector(X, 0, 0));
			Scheduler->RegisterTarget(Actor, Team);
			return Actor;
		}
		UProject_JTargetScoringComponent* Agent()
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
			NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
			auto* Component = NewObject<UProject_JTargetScoringComponent>(NPC);
			NPC->AddInstanceComponent(Component); Component->RegisterComponent(); Component->StartBatchedNPCDecisions(1);
			return Component;
		}
		void Tick() { Service->Tick(0); Scheduler->Tick(0); }
	};

	class FNativeNPCBatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNativeNPCBatchCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			Enemy = Fixture.Target(100, 2);
			Fixture.Target(20, 1); // Closer friendly must not enter the scoring snapshot.
			for (int32 Index = 0; Index < 96; ++Index) { Components.Add(Fixture.Agent()); }
			Seen.Init(false, Components.Num());
			Test->TestEqual(TEXT("Native NPCs registered"), Fixture.Scheduler->GetAgentCount(), 96);
			Components[0]->StartBatchedNPCDecisions(1);
			Test->TestEqual(TEXT("Duplicate registration is idempotent"), Fixture.Scheduler->GetAgentCount(), 96);
			Test->TestFalse(TEXT("Manual query cannot compete with registered batching"), Components[0]->RequestTargets({Enemy}));
			Fixture.Scheduler->Tick(0);
			Fixture.Service->Tick(0);
			Components[0]->GetOwner()->FindComponentByClass<UProject_JEquipmentManagerComponent>()->OnEquipmentEquipped.Broadcast(EProject_JEquipmentSlot::None, nullptr);
			Components[0]->StopBatchedNPCDecisions(); // Keep this agent stopped while its former batch companions finish.
		}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 15.0) { Test->AddError(TEXT("Native NPC fairness test timed out")); return true; }
			Fixture.Tick();
			const auto& Stats = Fixture.Scheduler->GetStats();
			Test->TestTrue(TEXT("Outstanding capacity bounded"), Fixture.Scheduler->GetOutstandingCount() <= Fixture.Scheduler->MaxOutstandingDecisions);
			Test->TestTrue(TEXT("GT result count bounded"), Stats.LastTickResults <= Fixture.Scheduler->MaxResultsPerTick);
			Test->TestTrue(TEXT("Agent scan bounded"), Stats.LastTickAgentVisits <= Fixture.Scheduler->MaxAgentVisitsPerTick);
			Test->TestTrue(TEXT("Candidate collection bounded"), Stats.LastTickCandidateVisits <= Fixture.Scheduler->MaxQueriesPerDispatch * Fixture.Scheduler->MaxTargets);
			for (int32 Index = 1; Index < Components.Num(); ++Index)
			{
				if (!Seen[Index] && Components[Index]->GetLastScoredTarget())
				{
					Test->TestEqual(TEXT("Only the enemy is selected"), Components[Index]->GetLastScoredTarget(), Enemy);
					Seen[Index] = true; ++SeenCount;
					Components[Index]->StopBatchedNPCDecisions();
				}
			}
			if (SeenCount != Components.Num() - 1) { return false; }
			Test->TestNull(TEXT("Cancelled agent cannot receive its old shared result"), Components[0]->GetLastScoredTarget());
			Test->TestTrue(TEXT("Late registered NPC was serviced"), Seen.Last());
			Test->TestTrue(TEXT("Multiple NPC queries share submissions"), Stats.SubmittedDecisions > Stats.SubmittedBatches);
			Test->TestEqual(TEXT("All unregistered agents release scheduler reservation"), Fixture.Scheduler->GetOutstandingCount(), 0);
			Test->AddInfo(FString::Printf(TEXT("NativeNPCBatch serviced=%d submitted_decisions=%llu root_batches=%llu discarded=%llu"), SeenCount, Stats.SubmittedDecisions, Stats.SubmittedBatches, Stats.DiscardedDecisions));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		FDecisionFixture Fixture;
		AActor* Enemy;
		TArray<UProject_JTargetScoringComponent*> Components;
		TArray<bool> Seen;
		int32 SeenCount = 0;
	};

	class FNPCRevalidationCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNPCRevalidationCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			Test->TestFalse(TEXT("Client does not schedule authoritative NPC decisions"), UProject_JNPCDecisionSubsystem::CanScheduleNetworkMode(NM_Client));
			Test->TestTrue(TEXT("Dedicated server supported"), UProject_JNPCDecisionSubsystem::CanScheduleNetworkMode(NM_DedicatedServer));
			Test->TestTrue(TEXT("Listen server supported"), UProject_JNPCDecisionSubsystem::CanScheduleNetworkMode(NM_ListenServer));
			Enemy = Fixture.Target(100, 2);
			A = Fixture.Agent(); B = Fixture.Agent();
			Fixture.Scheduler->Tick(0); Fixture.Service->Tick(0);
			Fixture.Scheduler->RegisterTarget(Enemy, 1); // Becomes friendly after snapshot.
			CastChecked<AProject_JNPCCharacter>(A->GetOwner())->GetAttributeSet()->InitHealth(0);
		}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 10.0) { Test->AddError(TEXT("NPC revalidation timed out")); return true; }
			Fixture.Tick();
			if (Fixture.Scheduler->GetStats().DiscardedDecisions < 2) { return false; }
			Test->TestNull(TEXT("Dead owner discards result"), A->GetLastScoredTarget());
			Test->TestNull(TEXT("Newly friendly target discards result"), B->GetLastScoredTarget());
			Fixture.World->BeginTearingDown();
			Test->TestEqual(TEXT("World teardown clears reservations"), Fixture.Scheduler->GetOutstandingCount(), 0);
			Test->TestEqual(TEXT("World teardown clears agents"), Fixture.Scheduler->GetAgentCount(), 0);
			Test->TestFalse(TEXT("World teardown rejects registration"), B->StartBatchedNPCDecisions(1));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		FDecisionFixture Fixture;
		AActor* Enemy;
		UProject_JTargetScoringComponent* A;
		UProject_JTargetScoringComponent* B;
	};

	class FNPCExperimentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNPCExperimentCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			Fixture.World->InitializeActorsForPlay(FURL());
			Fixture.World->GetWorldSettings()->NotifyBeginPlay();
			External = Fixture.Target(500, 3);
			Experiment = Fixture.World->SpawnActor<AProject_JNPCDecisionExperiment>();
			Test->TestTrue(TEXT("Native sandbox starts without assets"), Experiment->StartExperiment(4, 2));
			Test->TestTrue(TEXT("Sandbox is running through actual BeginPlay"), Experiment->HasActorBegunPlay());
			Test->TestFalse(TEXT("Running sandbox cannot duplicate its actors"), Experiment->StartExperiment(4, 2));
			Experiment->SetActorTickEnabled(false);
		}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 10.0) { Test->AddError(TEXT("NPC sandbox timed out")); return true; }
			Fixture.Tick();
			if (Fixture.Scheduler->GetStats().AppliedDecisions < 4) { return false; }
			Test->AddInfo(Experiment->GetSummary());
			Experiment->StopExperiment();
			Test->TestEqual(TEXT("Sandbox unregisters owned NPCs"), Fixture.Scheduler->GetAgentCount(), 0);
			Test->TestEqual(TEXT("Sandbox preserves external registry entry"), Fixture.Scheduler->GetTargetCount(), 1);
			Test->TestTrue(TEXT("Sandbox preserves external actor"), IsValid(External) && !External->IsActorBeingDestroyed());
			Fixture.World->EndPlay(EEndPlayReason::LevelTransition);
			Test->TestEqual(TEXT("EndPlay clears the remaining world registry"), Fixture.Scheduler->GetTargetCount(), 0);
			Test->TestFalse(TEXT("EndPlay rejects further sandbox starts"), Experiment->StartExperiment(4, 2));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started;
		FDecisionFixture Fixture;
		AActor* External;
		AProject_JNPCDecisionExperiment* Experiment;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNPCBatchTest, "ProjectJ.NPCDecision.NativeBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNativeNPCBatchTest::RunTest(const FString& Parameters) { ADD_LATENT_AUTOMATION_COMMAND(FNativeNPCBatchCommand(this)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCRevalidationTest, "ProjectJ.NPCDecision.Revalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPCRevalidationTest::RunTest(const FString& Parameters) { ADD_LATENT_AUTOMATION_COMMAND(FNPCRevalidationCommand(this)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCExperimentTest, "ProjectJ.NPCDecision.Experiment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPCExperimentTest::RunTest(const FString& Parameters) { ADD_LATENT_AUTOMATION_COMMAND(FNPCExperimentCommand(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpatialImportanceTest, "ProjectJ.NPCDecision.SpatialImportance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNPCSpatialImportanceTest::RunTest(const FString& Parameters)
{
	FDecisionFixture Fixture;
	Fixture.Target(100, 2);
	for (int32 Index = 0; Index < 32; ++Index) { Fixture.Target(100000 + Index * 1000, 2); }
	AActor* Observer = Fixture.Target(20000, 1);
	TestTrue(TEXT("Explicit server observer registered"), Fixture.Scheduler->RegisterObserver(Observer));
	TArray<UProject_JTargetScoringComponent*> Components;
	for (int32 Index = 0; Index < 12; ++Index) { Components.Add(Fixture.Agent()); }
	auto* NPC = CastChecked<AProject_JNPCCharacter>(Components[0]->GetOwner());
	const float PreviousSignificance = NPC->GetSignificance();
	Fixture.Scheduler->Tick(0); // Queue only: test the GT producer independently of task completion timing.
	const auto InitialStats = Fixture.Scheduler->GetStats();
	TestEqual(TEXT("Single shared snapshot per collection pass"), InitialStats.LastTickSnapshotBuilds, 1);
	TestEqual(TEXT("Registered target positions sampled once, not once per NPC"), InitialStats.LastTickTargetPositionReads, Fixture.Scheduler->GetTargetCount());
	TestTrue(TEXT("Spatial lookup prunes remote targets"), InitialStats.LastTickCandidateVisits < 12 * Fixture.Scheduler->GetTargetCount());
	EProject_JNPCUpdateBudgetTier Tier = EProject_JNPCUpdateBudgetTier::Near;
	double Interval = 0;
	TestTrue(TEXT("Registered agent exposes its local budget"), Fixture.Scheduler->GetAgentDecisionBudget(Components[0], Tier, Interval));
	TestTrue(TEXT("Distant observer selects hidden-distance tier"), Tier == EProject_JNPCUpdateBudgetTier::Hidden);
	TestEqual(TEXT("Existing hidden interval reused"), Interval, 1.0);
	Observer->SetActorLocation(FVector::ZeroVector);
	Fixture.Scheduler->Tick(0);
	Fixture.Scheduler->GetAgentDecisionBudget(Components[0], Tier, Interval);
	TestTrue(TEXT("Approaching observer promotes before old interval expires"), Tier == EProject_JNPCUpdateBudgetTier::Near);
	TestEqual(TEXT("Near cadence is bounded at 50ms"), Interval, 0.05);
	Observer->SetActorLocation(FVector(20000, 0, 0));
	Components[0]->bUseUrgentNPCDecisionInterval = true;
	Fixture.Scheduler->Tick(0);
	Fixture.Scheduler->GetAgentDecisionBudget(Components[0], Tier, Interval);
	TestTrue(TEXT("Explicit urgency keeps decision near"), Tier == EProject_JNPCUpdateBudgetTier::Near);
	Components[0]->bUseUrgentNPCDecisionInterval = false;
	Fixture.Scheduler->UnregisterObserver(Observer);
	Fixture.Scheduler->Tick(0);
	Fixture.Scheduler->GetAgentDecisionBudget(Components[0], Tier, Interval);
	TestTrue(TEXT("No observers falls back conservatively"), Tier == EProject_JNPCUpdateBudgetTier::Near && Fixture.Scheduler->GetStats().bObserverCoverageIncomplete);
	TestEqual(TEXT("Consumer-local tiers do not mutate global significance"), NPC->GetSignificance(), PreviousSignificance);
	TestTrue(TEXT("Demotion hysteresis holds near inside the margin"), NPC->GetDecisionTierForDistance(2600, EProject_JNPCUpdateBudgetTier::Near) == EProject_JNPCUpdateBudgetTier::Near);
	TestTrue(TEXT("Outside margin demotes to mid"), NPC->GetDecisionTierForDistance(2800, EProject_JNPCUpdateBudgetTier::Near) == EProject_JNPCUpdateBudgetTier::Mid);
	TestTrue(TEXT("Mid does not oscillate back inside demotion margin"), NPC->GetDecisionTierForDistance(2600, EProject_JNPCUpdateBudgetTier::Mid) == EProject_JNPCUpdateBudgetTier::Mid);
	TestTrue(TEXT("Promotion uses the base boundary"), NPC->GetDecisionTierForDistance(2400, EProject_JNPCUpdateBudgetTier::Mid) == EProject_JNPCUpdateBudgetTier::Near);
	Fixture.World->BeginTearingDown();
	TestFalse(TEXT("Teardown rejects observer registration"), Fixture.Scheduler->RegisterObserver(Observer));
	return true;
}
#endif
