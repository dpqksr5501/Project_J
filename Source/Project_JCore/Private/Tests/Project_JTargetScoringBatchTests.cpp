#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	TArray<ProjectJ::TargetScoring::FSnapshot> MakeQueries(int32 NumQueries = 32)
	{
		FRandomStream Random(20260908);
		TArray<ProjectJ::TargetScoring::FSnapshot> Queries;
		for (int32 Query = 0; Query < NumQueries; ++Query)
		{
			auto& Snapshot = Queries.AddDefaulted_GetRef();
			Snapshot.Origin = FVector(Query * 10, 0, 0);
			for (int32 Candidate = 0; Candidate < 256; ++Candidate)
			{
				Snapshot.Candidates.Add({Candidate, FVector(Random.FRandRange(-2000.f, 2000.f), Random.FRandRange(-2000.f, 2000.f), 0)});
			}
		}
		return Queries;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTargetBatchMathTest, "ProjectJ.AsyncTargeting.BatchMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTargetBatchMathTest::RunTest(const FString& Parameters)
{
	using namespace ProjectJ::TargetScoring;
	std::atomic<bool> Cancelled{false};
	auto Queries = MakeQueries();
	const auto Serial = EvaluateBatch(Queries, false, Cancelled);
	const auto Parallel = EvaluateBatch(Queries, true, Cancelled);
	TestEqual(TEXT("Every query returns a result"), Parallel.Results.Num(), Queries.Num());
	for (int32 Index = 0; Index < Queries.Num(); ++Index)
	{
		const auto Single = Evaluate(Queries[Index], false, Cancelled);
		TestEqual(TEXT("Batch serial equals single serial"), Serial.Results[Index].BestScore, Single.BestScore);
		TestEqual(TEXT("Batch parallel preserves winner"), Parallel.Results[Index].BestId, Single.BestId);
		TestEqual(TEXT("Batch parallel preserves score"), Parallel.Results[Index].BestScore, Single.BestScore);
		TestEqual(TEXT("Batch parallel preserves eligibility"), Parallel.Results[Index].EligibleCandidates, Single.EligibleCandidates);
	}
	Cancelled.store(true);
	for (const auto& Result : EvaluateBatch(Queries, true, Cancelled).Results)
	{
		TestTrue(TEXT("Whole-batch cancellation reaches every query"), Result.Status == EStatus::Cancelled);
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Service = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
	const auto Callback = [](const FProjectJTargetScoringBatchCompletion&) {};
	TestFalse(TEXT("Empty batch rejected"), Service->SubmitBatch(Service, {}, EProject_JTargetScoringExecution::TaskParallelFor, 0, Callback).IsValid());
	auto TooMany = MakeQueries(65);
	TestFalse(TEXT("Oversized query count rejected"), Service->SubmitBatch(Service, MoveTemp(TooMany), EProject_JTargetScoringExecution::TaskParallelFor, 0, Callback).IsValid());
	auto TooLarge = MakeQueries(64);
	TooLarge[0].Candidates.Add({256, FVector::ZeroVector});
	TestFalse(TEXT("Total candidate bound includes all queries"), Service->SubmitBatch(Service, MoveTemp(TooLarge), EProject_JTargetScoringExecution::TaskParallelFor, 0, Callback).IsValid());
	TestTrue(TEXT("Valid batch consumes one slot"), Service->SubmitBatch(Service, MoveTemp(Queries), EProject_JTargetScoringExecution::TaskParallelFor, 0, Callback).IsValid());
	TestEqual(TEXT("Single shared admission slot"), Service->GetPendingCount(), 1);
	Service->CancelForOwner(Service);
	World->DestroyWorld(false);
	return true;
}

namespace
{
	class FProjectJBatchDispatchBenchmark : public IAutomationLatentCommand
	{
	public:
		explicit FProjectJBatchDispatchBenchmark(FAutomationTestBase* InTest) : Test(InTest), Queries(MakeQueries())
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Service = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
			std::atomic<bool> Cancelled{false};
			Expected = ProjectJ::TargetScoring::EvaluateBatch(Queries, false, Cancelled);
			Started = FPlatformTime::Seconds();
			StartRound();
		}
		~FProjectJBatchDispatchBenchmark() override { World->DestroyWorld(false); }
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 30.0) { Test->AddError(TEXT("Batch comparison timed out")); return true; }
			Service->Tick(0);
			if (Completed == Queries.Num())
			{
				const uint64 Tasks = Service->GetLaunchedTaskCount() - TasksBefore;
				Test->TestEqual(TEXT("Root tasks match the selected submission strategy"), Tasks, uint64(Mode == 0 ? Queries.Num() : 1));
				if (Round > 0)
				{
					Elapsed.Add((FPlatformTime::Seconds() - RoundStarted) * 1000.0);
					SubmitTimes.Add(SubmitMicroseconds);
				}
				if (++Round == 6)
				{
					Elapsed.Sort(); SubmitTimes.Sort();
					Test->AddInfo(FString::Printf(TEXT("NPCBatchComparison mode=%s queries=32 candidates_per_query=256 samples=5 root_tasks_per_round=%llu delivery_ms_p50=%.3f delivery_ms_p95=%.3f submit_gt_us_p50=%.3f"),
						Mode == 0 ? TEXT("Individual") : TEXT("Batch"), Tasks, Elapsed[2], Elapsed[4], SubmitTimes[2]));
					if (++Mode == 2) { return true; }
					Round = 0; Elapsed.Empty(); SubmitTimes.Empty();
				}
				StartRound();
			}
			if (Mode == 0)
			{
				while (Issued < Queries.Num() && Service->GetPendingCount() < Service->MaxPendingRequests)
				{
					const int32 Index = Issued++;
					const double Before = FPlatformTime::Seconds();
					const auto Token = Service->Submit(Service, Queries[Index], EProject_JTargetScoringExecution::TaskParallelFor, 0,
						[this, Index](const FProjectJTargetScoringCompletion& Result) { CheckResult(Index, Result.Result); ++Completed; }, 10.0);
					SubmitMicroseconds += (FPlatformTime::Seconds() - Before) * 1.e6;
					if (!Token.IsValid()) { Test->AddError(TEXT("Individual comparison rejected")); return true; }
				}
			}
			else if (Issued == 0)
			{
				Issued = Queries.Num();
				const double Before = FPlatformTime::Seconds();
				const auto Token = Service->SubmitBatch(Service, Queries, EProject_JTargetScoringExecution::TaskParallelFor, 0,
					[this](const FProjectJTargetScoringBatchCompletion& Result)
					{
						Test->TestEqual(TEXT("Complete batch result count"), Result.Result.Results.Num(), Queries.Num());
						for (int32 Index = 0; Index < Result.Result.Results.Num(); ++Index) { CheckResult(Index, Result.Result.Results[Index]); }
						Completed = Queries.Num();
					}, 10.0);
				SubmitMicroseconds += (FPlatformTime::Seconds() - Before) * 1.e6;
				if (!Token.IsValid()) { Test->AddError(TEXT("Batch comparison rejected")); return true; }
			}
			return false;
		}
	private:
		void StartRound() { Completed = Issued = 0; SubmitMicroseconds = 0; RoundStarted = FPlatformTime::Seconds(); TasksBefore = Service->GetLaunchedTaskCount(); }
		void CheckResult(int32 Index, const ProjectJ::TargetScoring::FResult& Result)
		{
			Test->TestTrue(TEXT("Result on GT and completed"), IsInGameThread() && Result.Status == ProjectJ::TargetScoring::EStatus::Completed);
			Test->TestEqual(TEXT("Submission strategies preserve serial winner"), Result.BestId, Expected.Results[Index].BestId);
			Test->TestEqual(TEXT("Submission strategies preserve serial score"), Result.BestScore, Expected.Results[Index].BestScore);
		}
		FAutomationTestBase* Test;
		UWorld* World;
		UProject_JTargetScoringSubsystem* Service;
		TArray<ProjectJ::TargetScoring::FSnapshot> Queries;
		ProjectJ::TargetScoring::FBatchResult Expected;
		double Started = 0, RoundStarted = 0, SubmitMicroseconds = 0;
		uint64 TasksBefore = 0;
		int32 Mode = 0, Round = 0, Issued = 0, Completed = 0;
		TArray<double> Elapsed, SubmitTimes;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBatchDispatchBenchmarkTest, "ProjectJ.AsyncTargeting.BatchDispatchComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBatchDispatchBenchmarkTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FProjectJBatchDispatchBenchmark(this));
	return true;
}
#endif
