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
#endif
