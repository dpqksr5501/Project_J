#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Optimization/Project_JRetryDelay.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/MiscTrace.h"

TRACE_DECLARE_INT_COUNTER(NavLoadPhase, TEXT("ProjectJ/NavLoad/Phase"));
TRACE_DECLARE_INT_COUNTER(NavLoadBuildRemaining, TEXT("ProjectJ/NavLoad/BuildRemaining"));
TRACE_DECLARE_INT_COUNTER(NavLoadBuildRunning, TEXT("ProjectJ/NavLoad/BuildRunning"));
TRACE_DECLARE_INT_COUNTER(NavLoadArrived, TEXT("ProjectJ/NavLoad/Arrived"));

namespace
{
// Transient native Characters, real Recast and real scheduler; no authored level or asset writes.
// Query-only stages stop movement. Pursuit includes scoring/actions/movement, excludes combat and rendering.
class FNavLoadCommand : public IAutomationLatentCommand
{
public:
	explicit FNavLoadCommand(FAutomationTestBase* InTest, int32 InCount = 100) : Count(InCount), Test(InTest), Started(FPlatformTime::Seconds())
	{
		Test->AddExpectedMessage(TEXT("SpawnMissingNavigationData: No saved navigation data found"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		Test->AddExpectedMessage(TEXT("Unable to find RecastNavMesh instance while trying to create UCrowdManager instance"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
		FString Root;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectJNavOutput="), Root))
		{
			Root = FPaths::ProjectSavedDir() / TEXT("Validation/GroupC") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		}
		Output = FPaths::ConvertRelativePathToFull(Root);
		if (Count != 100) { Output /= FString::Printf(TEXT("Burst%d"), Count); }
		IFileManager::Get().MakeDirectory(*Output, true);
		Frames = TEXT("phase,frame,wall_s,world_tick_ms,policy_ms,requests,queued,inflight,tombstones,accepted,rejected,dispatched,delivered,cancelled,expired,discarded,build_remaining,build_running,arrived,action_failed,action_stale,retry_scheduled\n");
		Results = TEXT("phase,owner,status,queue_ms,engine_observed_ms,delivery_ms,total_ms,token\n");
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		auto Box = [&](FVector Location, FVector Scale, bool bMovable)
		{
			auto* Actor = World->SpawnActor<AActor>();
			auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
			Actor->SetRootComponent(Mesh); Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			// A closed triangle surface can leave disconnected walkable space inside the box.
			// This fixture models solid obstacles, so also exclude navigation under their top surface.
			Mesh->bFillCollisionUnderneathForNavmesh = bMovable;
			Mesh->SetMobility(bMovable ? EComponentMobility::Movable : EComponentMobility::Static);
			Mesh->RegisterComponent(); Actor->SetActorLocation(Location); Actor->SetActorScale3D(Scale);
			return Actor;
		};
		Box(FVector(0, 0, -50), FVector(100, 60, 1), false);
		Obstacle = Box(FVector(0, 0, 150), FVector(4, 12, 3), true);
		for (int32 I = 0; I < 8; ++I)
		{
			StormObstacles.Add(Box(FVector(-1400 + (I % 4) * 900, I < 4 ? -1800 : 1800, 150), FVector(2.5, 2.5, 3), true));
		}
		auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
		auto* Builder = NewObject<UCubeBuilder>(); Builder->X = 9800; Builder->Y = 5800; Builder->Z = 1000;
		UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::GameMode);
		Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (Nav)
		{
			Nav->OnNavigationBoundsUpdated(Bounds);
			if (auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::Create))
			{
				auto* Property = FindFProperty<FEnumProperty>(ANavigationData::StaticClass(), TEXT("RuntimeGeneration"));
				Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Data), int64(ERuntimeGenerationType::Dynamic));
				Data->MarkRequiresInitialRebuild();
			}
			Nav->Build(); // Setup only. Measured dirty stage never calls Build/invalidate/AddDirtyArea.
		}
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < Count; ++I)
		{
			auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),
				Count > 100 ? FVector(-4200 + (I % 32) * 75, -1400 + (I / 32) * 180, 100)
					: FVector(-3200 + (I % 10) * 110, -900 + (I / 10) * 180, 100), FRotator::ZeroRotator, P);
			auto* AI = World->SpawnActor<AAIController>(); AI->Possess(NPC);
			NPC->GetMesh()->SetCanEverAffectNavigation(false);
			NPC->GetCapsuleComponent()->SetCanEverAffectNavigation(false);
			NPC->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			NPC->GetCharacterMovement()->MaxWalkSpeed = 600;
			NPC->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths = true;
			NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
			NPC->GetAttributeSet()->InitHealth(100);
			NPCs.Add(NPC);
			PawnIndices.Add(NPC->GetUniqueID(), I);
		}
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		Service = World->GetSubsystem<UProject_JNPCPathSubsystem>();
		Tokens.Init(0, Count); Done.Init(false, Count); RetryAt.Init(0, Count); Attempts.Init(0, Count);
		BeginPhase(0, TEXT("Setup"));
	}
	~FNavLoadCommand()
	{
		if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); }
		if (!bEnded)
		{
			// Complete the normal post-actor-tick query barrier before navigation data cleanup.
			World->Tick(LEVELTICK_All, Step);
			World->EndPlay(EEndPlayReason::LevelTransition);
		}
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	bool Update() override
	{
		if (!Nav || !Service) { return Finish(false, TEXT("Initialization failed")); }
		if (FPlatformTime::Seconds() - PhaseStarted > 60) { return Finish(false, TEXT("Phase timed out")); }
		if (Phase == 7)
		{
			// Exercise the project EndPlay hook while engine navigation data is still alive.
			// World::Tick includes the engine's post-actor-tick async query barrier; ticking Nav
			// after World::EndPlay would reuse cleaned-up data and is not a valid engine lifecycle.
			{ TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NavLoad_PostServiceStop); World->Tick(LEVELTICK_All, Step); }
			if (FPlatformTime::Seconds() - PhaseStarted < 1.0) { return false; }
			Test->TestTrue(TEXT("Observed real engine completion rejected after project service EndPlay hook"), Service->GetStats().IgnoredAfterStop > 0);
			Test->TestEqual(TEXT("Post-stop observation leaves no project requests"), Service->GetRequestCount(), 0);
			Test->TestEqual(TEXT("No user callback during post-stop observation"), ForbiddenCallbacks, 0);
			World->EndPlay(EEndPlayReason::LevelTransition); bEnded = true;
			Test->TestEqual(TEXT("Actual world end leaves no project requests"), Service->GetRequestCount(), 0);
			Test->TestEqual(TEXT("Actual world end releases action registry"), World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->GetActionCount(), 0);
			return Finish(!Test->HasAnyErrors(), TEXT("Completed"));
		}
		if (Phase == 1 || Phase == 2) { SubmitPending(); }
		if (Phase == 4 || Phase == 9)
		{
			Simulated += Step;
			Target->SetActorLocation(FVector(Phase == 4 ? 2500 : -3000, FMath::Min(Simulated * 100.0, 800.0), 90));
		}
		if (Phase == 8 || Phase == 9) { MoveStormObstacles(); }
		const double TickStart = FPlatformTime::Seconds();
		const uint64 PreviousTick = Service->GetStats().TickSequence;
		{ TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NavLoad_WorldTick); World->Tick(LEVELTICK_All, Step); }
		const double TickMs = (FPlatformTime::Seconds() - TickStart) * 1000.0;
		const auto& S = Service->GetStats();
		DrainLatencySamples();
		const int32 Remaining = Nav->GetNumRemainingBuildTasks(), Running = Nav->GetNumRunningBuildTasks();
		if (Phase == 3 || Phase == 8 || Phase == 9) { DirtyPeak = FMath::Max(DirtyPeak, Remaining); }
		if (Phase == 9 && Remaining > 0 && Service->GetRequestCount() > 0) { ++MixedPressureFrames; }
		int32 Arrived = 0;
		uint64 ActionFailed = 0, ActionStale = 0, RetryScheduled = 0;
		for (const auto* Action : Actions)
		{
			Arrived += Action->GetActionState() == EProjectJNPCActionState::InRange ? 1 : 0;
			ActionFailed += Action->GetStats().PathFailed; ActionStale += Action->GetStats().PathStale;
			RetryScheduled += Action->GetStats().RetryScheduled;
		}
		TRACE_COUNTER_SET(NavLoadBuildRemaining, Remaining); TRACE_COUNTER_SET(NavLoadBuildRunning, Running);
		TRACE_COUNTER_SET(NavLoadArrived, Arrived);
		Frames += FString::Printf(TEXT("%s,%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%llu,%llu,%llu\n"),
			*PhaseName, Frame++, FPlatformTime::Seconds() - Started, TickMs, PreviousTick == S.TickSequence ? 0 : S.LastTickMilliseconds, Service->GetRequestCount(), S.Queued,
			S.InFlight, S.Tombstones, S.Accepted, S.Rejected, S.Dispatched, S.Delivered, S.Cancelled, S.Expired, S.Discarded, Remaining, Running, Arrived,
			ActionFailed, ActionStale, RetryScheduled);
		if (S.InFlight > Service->MaxInFlight || Service->GetRequestCount() > Service->MaxRequests
			|| S.LastTickDispatches > Service->MaxDispatchesPerTick || S.LastTickDeliveries > Service->MaxDeliveriesPerTick)
		{ return Finish(false, TEXT("Service budget exceeded")); }
		if (Phase == 0)
		{
			FNavLocation Position;
			if (!Nav->IsNavigationBuildInProgress() && Nav->ProjectPointToNavigation(FVector(2500, 0, 0), Position))
			{
				Goal = Position.Location;
				if (Nav->ProjectPointToNavigation(FVector(0, 0, 0), Position, FVector(30, 30, 100)))
				{
					if (FPlatformTime::Seconds() - PhaseStarted < 2.0) { return false; }
					auto* Mesh = CastChecked<UStaticMeshComponent>(Obstacle->GetRootComponent());
					const auto* Element = Nav->GetDataForElement(FNavigationElementHandle(Mesh));
					FHitResult Hit; World->LineTraceSingleByChannel(Hit, FVector(0, 0, 1000), FVector(0, 0, -200), ECC_Visibility);
					Test->AddInfo(FString::Printf(TEXT("Obstacle diagnostic: bounds=%s scale=%s collision=%d navrelevant=%d octree=%d geometry=%d modifiers=%d rayhit=%s point=%s"),
						*Mesh->Bounds.GetBox().ToString(), *Mesh->GetComponentScale().ToString(), int32(Mesh->GetCollisionEnabled()), Mesh->IsNavigationRelevant(),
						Element != nullptr, Element ? Element->HasGeometry() : 0, Element ? Element->HasModifiers() : 0, *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToString()));
					Test->AddError(FString::Printf(TEXT("Obstacle center unexpectedly projects to %s; initial geometry not established"), *Position.Location.ToString()));
					return Finish(false, TEXT("Obstacle fixture invalid"));
				}
				BeginPhase(1, *FString::Printf(TEXT("Burst%d"), Count));
			}
		}
		else if ((Phase == 1 || Phase == 2) && !Done.Contains(false) && Service->GetRequestCount() == 0)
		{
			if (Phase == 1)
			{
				Test->TestEqual(TEXT("All admitted owners eventually complete real paths"), Successes, Count);
				Test->TestTrue(TEXT("Simultaneous burst exercises bounded rejection"), S.Rejected > 0);
				// The larger case isolates admission/fair retry; the 100-character fixture
				// continues through moving targets, dynamic navigation and lifecycle phases.
				if (Count != 100) { return Finish(true, TEXT("All crowd requests drained")); }
				ResetRequests(); BeginPhase(2, TEXT("Unreachable100"));
			}
			else
			{
				Test->TestEqual(TEXT("All 100 unreachable goals fail without partial movement"), Failures, Count);
				BeginPhase(3, TEXT("AutomaticDirty"));
				DirtyDispatches = S.Dispatched;
				Obstacle->SetActorLocation(FVector(0, 1600, 150));
			}
		}
		else if (Phase == 3)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NavLoad_GeometryProbe);
			FNavLocation OldPosition, NewPosition;
			if (Nav->ProjectPointToNavigation(FVector(0, 0, 0), OldPosition, FVector(30, 30, 100))
				&& !Nav->ProjectPointToNavigation(FVector(0, 1600, 0), NewPosition, FVector(30, 30, 100))
				&& !Nav->IsNavigationBuildInProgress())
			{
				Test->TestEqual(TEXT("Dirty tile stage dispatches no project path queries"), S.Dispatched, DirtyDispatches);
				Test->AddInfo(FString::Printf(TEXT("Automatic geometry dirty rebuild: %.3fs peak_remaining=%d; no explicit Build/invalidate."),
					FPlatformTime::Seconds() - PhaseStarted, DirtyPeak));
				BeginPhase(8, TEXT("DirtyStorm")); DirtyDispatches = S.Dispatched; StormRounds = 0; NextStormTime = 0;
			}
		}
		else if (Phase == 8 && StormRounds == 12 && AreStormTilesSettled())
		{
			Test->TestEqual(TEXT("Repeated eight-obstacle storm stays query-free"), S.Dispatched, DirtyDispatches);
			BeginPursuit();
		}
		else if (Phase == 4 && Simulated >= 8 && Arrived == Count)
		{
			Test->TestTrue(TEXT("Pursuit delivery latencies captured"), PursuitSamples > 0);
			Test->TestTrue(TEXT("100 consumers repath for moving goal"), S.Dispatched > PursuitDispatches + Count);
			BeginPhase(9, TEXT("Mixed100")); Simulated = 0; StormRounds = 0; NextStormTime = 0;
		}
		else if (Phase == 9 && StormRounds == 12 && Simulated >= 8 && Arrived == Count && AreStormTilesSettled())
		{
			Test->TestTrue(TEXT("Mixed stage observes pending path requests and tile work together"), MixedPressureFrames > 0);
			Test->TestTrue(TEXT("Mixed pursuit delivery latencies captured"), MixedSamples > 0);
			for (auto* Action : Actions) { Action->StopActions(); }
			for (auto* Scoring : Scorers) { Scoring->StopBatchedNPCDecisions(); }
			BeginPhase(5, TEXT("DrainPursuit"));
		}
		else if (Phase == 5 && Service->GetRequestCount() == 0)
		{
			BeginPhase(6, TEXT("CancelDestroy"));
			for (int32 I = 0; I < 32; ++I)
			{
				Tokens[I] = Service->Submit(NPCs[I], NPCs[I], Goal, 9, [this](const auto&) { ++ForbiddenCallbacks; });
			}
			Service->Tick(0); // Dispatch some actual engine jobs, cancel before next engine Tick.
			for (int32 I = 0; I < 32; ++I)
			{
				if (I % 2 == 0) { Service->Cancel(Tokens[I]); }
				else { NPCs[I]->Destroy(); }
			}
		}
		else if (Phase == 6 && Service->GetRequestCount() == 0)
		{
			Test->TestEqual(TEXT("Cancelled/destroyed owners receive no callback"), ForbiddenCallbacks, 0);
			BeginPhase(7, TEXT("Teardown"));
			for (int32 I = 40; I < 60; ++I)
			{
				Service->Submit(NPCs[I], NPCs[I], Goal, 10, [this](const auto&) { ++ForbiddenCallbacks; });
			}
			Service->Tick(0);
			Nav->Tick(Step); // Move requests into an engine-owned batch before project cancellation.
			Service->OnWorldEndPlay(*World); // Stop project ownership before engine delivery, retaining valid Nav data.
			Test->TestEqual(TEXT("Project EndPlay hook releases queued/dispatched requests"), Service->GetRequestCount(), 0);
		}
		return false;
	}
private:
	void BeginPhase(int32 Value, const TCHAR* Name)
	{
		if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); }
		Phase = Value; PhaseName = Name; PhaseStarted = FPlatformTime::Seconds();
		const FString RegionName = TEXT("ProjectJ.NavLoad.") + PhaseName;
		RegionId = TRACE_BEGIN_REGION_WITH_ID(*RegionName);
		PhaseNames.Add(Value, PhaseName);
		if (Service) { Service->SetLatencyCapturePhaseForTest(Value); }
		const int32 RequestsInPhase = Value == 6 ? 32 : (Value == 7 ? 20 : (Value == 1 || Value == 2 || Value == 4 || Value == 9 ? 100 : 0));
		TRACE_BOOKMARK(TEXT("ProjectJ.NavLoad %s RequestOwners=%d"), Name, RequestsInPhase); TRACE_COUNTER_SET(NavLoadPhase, Phase);
		Test->AddInfo(FString::Printf(TEXT("NavLoad phase=%s"), Name));
	}
	void MoveStormObstacles()
	{
		const double Now = FPlatformTime::Seconds();
		if (StormRounds >= 12 || Now < NextStormTime) { return; }
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NavLoad_ObstacleMutations);
		for (int32 I = 0; I < StormObstacles.Num(); ++I)
		{
			const double Y = (I < 4 ? -1.0 : 1.0) * (StormRounds % 2 == 0 ? 600.0 : 1800.0);
			StormObstacles[I]->SetActorLocation(FVector(-1400 + (I % 4) * 900, Y, 150));
		}
		++StormRounds; ++TotalStormRounds; NextStormTime = Now + 0.1;
		TRACE_BOOKMARK(TEXT("ProjectJ.NavLoad %s ObstacleRound=%d Count=8"), *PhaseName, StormRounds);
	}
	bool AreStormTilesSettled() const
	{
		// A transform may be pending in the dirty-area controller before build jobs appear.
		// Verify geometry too, so a temporary zero job count cannot leak rebuild work into the next stage.
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NavLoad_GeometryProbe);
		if (Nav->IsNavigationBuildInProgress()) { return false; }
		for (int32 I = 0; I < StormObstacles.Num(); ++I)
		{
			const double Sign = I < 4 ? -1.0 : 1.0;
			FNavLocation Position;
			if (!Nav->ProjectPointToNavigation(FVector(-1400 + (I % 4) * 900, Sign * 600, 0), Position, FVector(30, 30, 100))
				|| Nav->ProjectPointToNavigation(FVector(-1400 + (I % 4) * 900, Sign * 1800, 0), Position, FVector(30, 30, 100)))
			{ return false; }
		}
		return true;
	}
	void DrainLatencySamples()
	{
		TArray<FProjectJNPCPathLatencySample> Samples;
		Service->DrainLatencySamplesForTest(Samples);
		for (const auto& S : Samples)
		{
			const int32* Index = PawnIndices.Find(S.PawnId);
			Results += FString::Printf(TEXT("%s,%d,%d,%.6f,%.6f,%.6f,%.6f,%llu\n"), *PhaseNames.FindChecked(S.Phase),
				Index ? *Index : INDEX_NONE, int32(S.Status), S.QueueMilliseconds, S.EngineMilliseconds,
				S.DeliveryMilliseconds, S.TotalMilliseconds, S.Token);
			PursuitSamples += S.Phase == 4 ? 1 : 0;
			MixedSamples += S.Phase == 9 ? 1 : 0;
		}
	}
	void ResetRequests() { Tokens.Init(0, Count); Done.Init(false, Count); RetryAt.Init(0, Count); Attempts.Init(0, Count); }
	void SubmitPending()
	{
		const double Now = FPlatformTime::Seconds();
		for (int32 I = 0; I < Count; ++I)
		{
			if (Done[I] || Tokens[I] || Now < RetryAt[I]) { continue; }
			Tokens[I] = Service->Submit(NPCs[I], NPCs[I], Phase == 1 ? Goal : FVector(20000, 0, 0), Phase,
				[this, I](const FProjectJNPCPathCompletion& R)
				{
					Done[I] = true; Tokens[I] = 0;
					Successes += R.Status == EProjectJNPCPathStatus::Success ? 1 : 0;
					Failures += R.Status == EProjectJNPCPathStatus::Failed ? 1 : 0;
				});
			if (!Tokens[I]) { RetryAt[I] = Now + ProjectJ::RetryDelay(Attempts[I]++, I + 1, 0.1, 1.0); }
		}
	}
	void BeginPursuit()
	{
		BeginPhase(4, TEXT("Pursuit100")); PursuitDispatches = Service->GetStats().Dispatched;
		Target = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Target); Target->SetRootComponent(Root); Root->RegisterComponent();
		Target->SetActorLocation(FVector(2500, 0, 90));
		World->GetSubsystem<UProject_JNPCDecisionSubsystem>()->RegisterTarget(Target, 2);
		for (auto* NPC : NPCs)
		{
			auto* Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
			Scoring->Range = 10000; Scoring->bUseUrgentNPCDecisionInterval = true; Scoring->StartBatchedNPCDecisions(1); Scorers.Add(Scoring);
			auto* Action = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Action); Action->RegisterComponent();
			Action->RetryInterval = 0.1;
			Test->TestTrue(TEXT("100-NPC pursuit starts"), Action->StartActions(Scoring, {})); Actions.Add(Action);
		}
	}
	bool Finish(bool bSuccess, const TCHAR* Reason)
	{
		if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); RegionId = 0; }
		if (Service)
		{
			DrainLatencySamples();
			Test->TestEqual(TEXT("Bounded latency capture did not drop samples"), Service->GetDroppedLatencySamplesForTest(), uint64(0));
			bSuccess = bSuccess && !Test->HasAnyErrors();
		}
		TRACE_BOOKMARK(TEXT("ProjectJ.NavLoad Finish Success=%d"), bSuccess);
		if (!bSuccess) { Test->AddError(FString::Printf(TEXT("NavLoad %s: %s"), *PhaseName, Reason)); }
		Test->TestTrue(TEXT("Frames CSV written"), FFileHelper::SaveStringToFile(Frames, *(Output / TEXT("frames.csv"))));
		Test->TestTrue(TEXT("Requests CSV written"), FFileHelper::SaveStringToFile(Results, *(Output / TEXT("requests.csv"))));
		const auto S = Service ? Service->GetStats() : FProjectJNPCPathStats{};
		const FString Summary = FString::Printf(TEXT("success=%d\nphase=%s\nreason=%s\ncount=%d\nstep=0.05\nwall_s=%.6f\naccepted=%llu\nrejected=%llu\ndispatched=%llu\ndelivered=%llu\ncancelled=%llu\nexpired=%llu\ndiscarded=%llu\npeak_inflight=%d\npeak_queued=%d\nstopped_requests=%llu\nremaining=%d\nforbidden_callbacks=%d\n"),
			bSuccess, *PhaseName, Reason, Count, FPlatformTime::Seconds() - Started, S.Accepted, S.Rejected, S.Dispatched, S.Delivered,
			S.Cancelled, S.Expired, S.Discarded, S.PeakInFlight, S.PeakQueued, S.StoppedRequests, Service ? Service->GetRequestCount() : 0, ForbiddenCallbacks);
		const FString Observation = FString::Printf(TEXT("pursuit_latency_samples=%d\nmixed_latency_samples=%d\nmixed_pressure_frames=%d\nstorm_rounds=%d\nstorm_obstacles=8\ncancel_destroy_owners=32\nteardown_owners=20\nignored_engine_completions_after_stop=%llu\npost_service_stop_observation_s=%.6f\n"),
			PursuitSamples, MixedSamples, MixedPressureFrames, TotalStormRounds, S.IgnoredAfterStop, bEnded ? FPlatformTime::Seconds() - PhaseStarted : 0.0);
		Test->TestTrue(TEXT("Summary written"), FFileHelper::SaveStringToFile(Summary + Observation, *(Output / TEXT("summary.txt"))));
		Test->AddInfo(Summary + Observation); return true;
	}
	const int32 Count;
	static constexpr float Step = 0.05f;
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	UNavigationSystemV1* Nav = nullptr;
	UProject_JNPCPathSubsystem* Service = nullptr;
	AActor* Obstacle = nullptr;
	AActor* Target = nullptr;
	TArray<AProject_JNPCCharacter*> NPCs;
	TArray<AActor*> StormObstacles;
	TArray<UProject_JNPCActionComponent*> Actions;
	TArray<UProject_JTargetScoringComponent*> Scorers;
	TArray<uint64> Tokens;
	TArray<bool> Done;
	TArray<double> RetryAt;
	TArray<uint32> Attempts;
	TMap<uint32, int32> PawnIndices;
	TMap<int32, FString> PhaseNames;
	int32 PursuitSamples = 0;
	int32 MixedSamples = 0, StormRounds = 0, TotalStormRounds = 0, MixedPressureFrames = 0;
	double NextStormTime = 0;
	FVector Goal;
	FString Output, Frames, Results, PhaseName;
	double Started, PhaseStarted = 0, Simulated = 0;
	int32 Phase = 0, Frame = 0, Successes = 0, Failures = 0, ForbiddenCallbacks = 0, DirtyPeak = 0;
	uint64 DirtyDispatches = 0, PursuitDispatches = 0;
	uint64 RegionId = 0;
	bool bEnded = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCNavigationLoadTest, "ProjectJ.GroupC.NavigationLoad100",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCNavigationLoadTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNavLoadCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCNavigationCrowdTest, "ProjectJ.Crowd.NavigationBurst512",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCNavigationCrowdTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FNavLoadCommand(this, 512));
 return true;
}
#endif
