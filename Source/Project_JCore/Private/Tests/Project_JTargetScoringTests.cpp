#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include <limits>

namespace
{
	ProjectJ::TargetScoring::FSnapshot MakeSnapshot(int32 Count)
	{
		ProjectJ::TargetScoring::FSnapshot Snapshot;
		FRandomStream Random(20260908);
		Snapshot.Candidates.Reserve(Count);
		for (int32 Id = 0; Id < Count; ++Id)
		{
			Snapshot.Candidates.Add({Id, FVector(Random.FRandRange(-2500.f, 2500.f), Random.FRandRange(-2500.f, 2500.f), 0)});
		}
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTargetScoringMathTest, "ProjectJ.AsyncTargeting.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTargetScoringMathTest::RunTest(const FString& Parameters)
{
	using namespace ProjectJ::TargetScoring;
	std::atomic<bool> Cancelled{false};
	FSnapshot Snapshot;
	Snapshot.Range = 100.0;
	Snapshot.Candidates = {{5, FVector(50, 0, 0)}, {2, FVector(50, 0, 0)}, {1, FVector(-50, 0, 0)}, {0, FVector(101, 0, 0)}};
	const auto Result = Evaluate(Snapshot, false, Cancelled);
	TestEqual(TEXT("Equal scores prefer lowest stable id"), Result.BestId, 2);
	TestEqual(TEXT("Distance plus facing score"), Result.BestScore, 1.5);
	TestEqual(TEXT("Outside range excluded"), Result.EligibleCandidates, 3);
	Snapshot.Candidates.Add({9, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)});
	Snapshot.Candidates.Add({INDEX_NONE, FVector::ZeroVector});
	TestEqual(TEXT("Nonfinite positions and invalid ids excluded"), Evaluate(Snapshot, false, Cancelled).EligibleCandidates, 3);
	Snapshot.Range = -1.0;
	TestTrue(TEXT("Invalid range rejected"), Evaluate(Snapshot, false, Cancelled).Status == EStatus::InvalidInput);
	Snapshot = MakeSnapshot(MaxCandidates);
	const auto Serial = Evaluate(Snapshot, false, Cancelled);
	const auto Parallel = Evaluate(Snapshot, true, Cancelled);
	TestEqual(TEXT("Parallel reduction has exact serial id"), Parallel.BestId, Serial.BestId);
	TestEqual(TEXT("Parallel reduction has exact serial score"), Parallel.BestScore, Serial.BestScore);
	TestEqual(TEXT("Parallel eligibility matches"), Parallel.EligibleCandidates, Serial.EligibleCandidates);
	Cancelled.store(true);
	TestTrue(TEXT("Cancellation avoids a result"), Evaluate(Snapshot, true, Cancelled).Status == EStatus::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTargetScoringLifecycleTest, "ProjectJ.AsyncTargeting.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTargetScoringLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Subsystem = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
	if (!TestNotNull(TEXT("Game world creates subsystem"), Subsystem)) { World->DestroyWorld(false); return false; }
	TestFalse(TEXT("Idle subsystem does not tick"), Subsystem->IsTickable());
	int32 Callbacks = 0;
	const auto Callback = [&Callbacks](const FProjectJTargetScoringCompletion&) { ++Callbacks; };
	TArray<FProject_JGameplayAsyncRequestToken> Tokens;
	for (int32 Index = 0; Index < Subsystem->MaxPendingRequests; ++Index)
	{
		Tokens.Add(Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0, Callback));
		TestTrue(TEXT("Bounded slot accepted"), Tokens.Last().IsValid());
	}
	TestFalse(TEXT("Ninth query is rejected"), Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0, Callback).IsValid());
	Subsystem->CancelForOwner(Subsystem);
	TestEqual(TEXT("Queued cancellation releases capacity"), Subsystem->GetPendingCount(), 0);
	Subsystem->Tick(0);
	TestEqual(TEXT("Cancelled callbacks suppressed"), Callbacks, 0);
	FProject_JGameplayAsyncRequestToken Second;
	Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 17,
		[&](const FProjectJTargetScoringCompletion& Completion)
		{
			TestTrue(TEXT("Apply runs on game thread"), IsInGameThread());
			TestEqual(TEXT("Revision preserved"), Completion.ContextRevision, uint64(17));
			TestTrue(TEXT("World epoch preserved"), Completion.WorldEpoch == Subsystem->GetWorldEpoch());
			Subsystem->Cancel(Second);
			++Callbacks;
		});
	Second = Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0, Callback);
	Subsystem->Tick(0);
	TestEqual(TEXT("Earlier callback can cancel another already computed result"), Callbacks, 1);
	AActor* Owner = World->SpawnActor<AActor>();
	Subsystem->Submit(Owner, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0, Callback);
	Owner->Destroy();
	Subsystem->Tick(0);
	TestEqual(TEXT("Destroyed owner cannot receive result"), Callbacks, 1);
	bool bExpired = false;
	Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0,
		[&](const FProjectJTargetScoringCompletion& Completion) { bExpired = Completion.Result.Status == ProjectJ::TargetScoring::EStatus::Expired; }, 1.e-9);
	Subsystem->Tick(0);
	TestTrue(TEXT("Expired query reports expiry"), bExpired);
	const auto OldEpoch = Subsystem->GetWorldEpoch();
	Subsystem->Submit(Subsystem, MakeSnapshot(16384), EProject_JTargetScoringExecution::TaskParallelFor, 0, Callback);
	Subsystem->Tick(0); // Dispatch, then tear down without joining worker tasks.
	World->BeginTearingDown();
	TestEqual(TEXT("Teardown cancels before subsystem deinitialization"), Subsystem->GetPendingCount(), 0);
	TestFalse(TEXT("Teardown rejects new queries"), Subsystem->Submit(Subsystem, MakeSnapshot(1), EProject_JTargetScoringExecution::Serial, 0, Callback).IsValid());
	World->DestroyWorld(false);
	TestEqual(TEXT("World teardown suppresses pending callback"), Callbacks, 1);
	World = UWorld::CreateWorld(EWorldType::Game, false);
	Subsystem = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
	TestTrue(TEXT("Replacement world gets a different epoch"), Subsystem->GetWorldEpoch() != OldEpoch);
	TestEqual(TEXT("Replacement world starts empty"), Subsystem->GetPendingCount(), 0);
	World->DestroyWorld(false);
	return true;
}

namespace
{
	/** Real UE task dispatch, advanced by automation frames; no Wait on the game thread. */
	class FTargetScoringBenchmarkCommand : public IAutomationLatentCommand
	{
	public:
		FTargetScoringBenchmarkCommand(FAutomationTestBase* InTest, int32 Count)
			: Test(InTest), Snapshot(MakeSnapshot(Count)), Started(FPlatformTime::Seconds())
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Subsystem = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
			std::atomic<bool> Cancelled{false};
			Expected = ProjectJ::TargetScoring::Evaluate(Snapshot, false, Cancelled);
			// A cancelled in-flight task must remain counted until the worker publishes completion.
			const auto CancelToken = Subsystem->Submit(Subsystem, MakeSnapshot(16384), EProject_JTargetScoringExecution::TaskParallelFor, 0,
				[this](const FProjectJTargetScoringCompletion&) { Test->AddError(TEXT("Cancelled in-flight callback was delivered")); });
			Subsystem->Tick(0);
			Subsystem->Cancel(CancelToken);
			Test->TestEqual(TEXT("Running cancellation retains admission slot until collected"), Subsystem->GetPendingCount(), 1);
			Test->TestFalse(TEXT("Cancelled request no longer logically pending"), Subsystem->IsPending(CancelToken));
		}
		virtual ~FTargetScoringBenchmarkCommand() override { if (World) { World->DestroyWorld(false); } }
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 30.0)
			{
				Test->AddError(TEXT("Target scoring benchmark timed out"));
				return true;
			}
			Subsystem->Tick(0);
			if (bFailed || Mode == 3) { return true; }
			if (Subsystem->GetPendingCount() != 0) { return false; }
			const auto Token = Subsystem->Submit(Subsystem, Snapshot, static_cast<EProject_JTargetScoringExecution>(Mode), 0,
				[this](const FProjectJTargetScoringCompletion& Completion)
				{
					Test->TestTrue(TEXT("Worker result delivered on GT"), IsInGameThread());
					if (Completion.Result.Status != ProjectJ::TargetScoring::EStatus::Completed)
					{
						Test->AddError(TEXT("Benchmark query did not complete"));
						bFailed = true;
						return;
					}
					Test->TestEqual(TEXT("All modes preserve serial winner"), Completion.Result.BestId, Expected.BestId);
					Test->TestEqual(TEXT("All modes preserve exact serial score"), Completion.Result.BestScore, Expected.BestScore);
					Test->TestEqual(TEXT("All modes preserve eligibility count"), Completion.Result.EligibleCandidates, Expected.EligibleCandidates);
					if (Sample >= Warmups)
					{
						Compute.Add(Completion.Result.ComputeMicroseconds);
						Delivery.Add(Completion.DeliveryMilliseconds);
					}
					if (++Sample == Warmups + Samples)
					{
						Compute.Sort(); Delivery.Sort();
						Test->AddInfo(FString::Printf(TEXT("TargetScoringBenchmark candidates=%d mode=%d samples=%d compute_us_p50=%.3f compute_us_p95=%.3f delivery_ms_p50=%.3f delivery_ms_p95=%.3f"),
							Snapshot.Candidates.Num(), Mode, Samples, Compute[Samples / 2 - 1], Compute[Samples - 1], Delivery[Samples / 2 - 1], Delivery[Samples - 1]));
						++Mode; Sample = 0; Compute.Empty(); Delivery.Empty();
					}
				}, 10.0);
			if (!Token.IsValid()) { Test->AddError(TEXT("Benchmark submission rejected")); return true; }
			return false;
		}
	private:
		static constexpr int32 Warmups = 2;
		static constexpr int32 Samples = 12; // Nearest-rank p95 is the maximum for twelve samples.
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		UProject_JTargetScoringSubsystem* Subsystem = nullptr;
		ProjectJ::TargetScoring::FSnapshot Snapshot;
		ProjectJ::TargetScoring::FResult Expected;
		double Started;
		int32 Mode = 0;
		int32 Sample = 0;
		bool bFailed = false;
		TArray<double> Compute;
		TArray<double> Delivery;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FProjectJTargetScoringBenchmarkTest, "ProjectJ.AsyncTargeting.Benchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FProjectJTargetScoringBenchmarkTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (int32 Count : {256, 4096, 16384}) { Names.Add(FString::FromInt(Count)); Commands.Add(FString::FromInt(Count)); }
}
bool FProjectJTargetScoringBenchmarkTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTargetScoringBenchmarkCommand(this, FCString::Atoi(*Parameters)));
	return true;
}

#endif
