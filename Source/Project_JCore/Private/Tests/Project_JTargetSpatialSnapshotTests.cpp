#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "HAL/PlatformTime.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialEquivalenceTest, "ProjectJ.AsyncTargeting.SpatialEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialEquivalenceTest::RunTest(const FString& Parameters)
{
	using namespace ProjectJ::TargetScoring;
	FRandomStream Random(20260908);
	TArray<FSpatialTarget> Targets;
	for (int32 Id = 0; Id < 256; ++Id)
	{
		Targets.Add({Id, FVector(Random.FRandRange(-20000.f, 20000.f), Random.FRandRange(-20000.f, 20000.f), Random.FRandRange(-2000.f, 2000.f)), Id % 3});
	}
	FTargetSpatialSnapshot Snapshot;
	TestTrue(TEXT("Build bounded spatial snapshot"), Snapshot.Build(Targets));
	for (int32 Query = 0; Query < 16; ++Query)
	{
		const FVector Origin(Random.FRandRange(-10000.f, 10000.f), Random.FRandRange(-10000.f, 10000.f), 0);
		for (double Radius : {0.0, 500.0, 2000.0, 500000.0})
		{
			TArray<int32> Indices, ActualIds, ExpectedIds;
			FSpatialQueryStats Stats;
			TestTrue(TEXT("Valid query"), Snapshot.Query(Origin, Radius, 1, Indices, Stats));
			for (int32 Index : Indices) { ActualIds.Add(Snapshot.GetTargets()[Index].Id); }
			for (const auto& Target : Targets)
			{
				if (Target.Team != 1 && FVector::DistSquared(Target.Position, Origin) <= FMath::Square(Radius)) { ExpectedIds.Add(Target.Id); }
			}
			TestTrue(TEXT("Grid and fallback preserve full-scan results and stable order"), ActualIds == ExpectedIds);
			TestTrue(TEXT("Cell enumeration bounded"), Stats.CellsVisited <= FTargetSpatialSnapshot::MaxQueryCells);
			TestTrue(TEXT("Every target considered at most once"), Stats.CandidatesVisited <= Targets.Num());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialEdgeCasesTest, "ProjectJ.AsyncTargeting.SpatialEdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialEdgeCasesTest::RunTest(const FString& Parameters)
{
	using namespace ProjectJ::TargetScoring;
	FTargetSpatialSnapshot Snapshot;
	TestTrue(TEXT("Build boundary values"), Snapshot.Build({{9, FVector(-1000, 0, 0), 2}, {2, FVector(1000, 0, 0), 2},
		{3, FVector(0, 0, 1001), 2}, {1, FVector::ZeroVector, 1}, {4, FVector(100000, 100000, 0), 2}}));
	TArray<int32> Indices;
	FSpatialQueryStats Stats;
	Snapshot.Query(FVector::ZeroVector, 1000, 1, Indices, Stats);
	TestEqual(TEXT("Exact 3D sphere excludes vertical and friendly candidates"), Indices.Num(), 2);
	if (Indices.Num() == 2)
	{
		TestEqual(TEXT("Stable id order differs from cell visitation"), Snapshot.GetTargets()[Indices[0]].Id, 2);
		TestEqual(TEXT("Negative cell boundary included"), Snapshot.GetTargets()[Indices[1]].Id, 9);
	}
	TestTrue(TEXT("Remote cell is pruned"), Stats.CandidatesVisited < Snapshot.GetTargets().Num());
	Snapshot.Query(FVector::ZeroVector, 1000000, 1, Indices, Stats);
	TestTrue(TEXT("Large radius falls back without truncation"), Stats.bLinearFallback && Indices.Num() == 4);
	TestFalse(TEXT("Invalid radius rejected"), Snapshot.Query(FVector::ZeroVector, -1, 1, Indices, Stats));
	TestTrue(TEXT("Invalid query clears old results"), Indices.IsEmpty());
	const FVector Extreme(1.e20, -1.e20, 0);
	Snapshot.Build({{5, Extreme, 2}});
	Snapshot.Query(Extreme, 100, 1, Indices, Stats);
	TestTrue(TEXT("Finite coordinates outside integer grid remain queryable"), Stats.bLinearFallback && Indices.Num() == 1);
	Snapshot.Build({{5, FVector(10, 0, 0), 2}});
	Snapshot.Query(FVector::ZeroVector, 100, 1, Indices, Stats);
	TestEqual(TEXT("Initial nearby target"), Indices.Num(), 1);
	Snapshot.Build({{5, FVector(10000, 0, 0), 2}});
	Snapshot.Query(FVector::ZeroVector, 100, 1, Indices, Stats);
	TestTrue(TEXT("Rebuild does not retain moved target's previous cell"), Indices.IsEmpty());
	Snapshot.Build({{5, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 2}});
	TestTrue(TEXT("Nonfinite location excluded"), Snapshot.GetTargets().IsEmpty());
	TestFalse(TEXT("Duplicate stable ids rejected"), Snapshot.Build({{5, FVector::ZeroVector, 2}, {5, FVector::ZeroVector, 2}}));
	TArray<FSpatialTarget> TooMany;
	TooMany.SetNum(FTargetSpatialSnapshot::MaxEntries + 1);
	TestFalse(TEXT("Snapshot capacity enforced"), Snapshot.Build(MoveTemp(TooMany)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialOwnedInputTest, "ProjectJ.AsyncTargeting.SpatialOwnedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialOwnedInputTest::RunTest(const FString& Parameters)
{
	using namespace ProjectJ::TargetScoring;
	FTargetSpatialSnapshot Snapshot;
	TArray<FSpatialTarget> Input;
	Input.Reserve(FTargetSpatialSnapshot::MaxEntries);
	for (int32 Id = 0; Id < FTargetSpatialSnapshot::MaxEntries; ++Id)
	{
		Input.Add({Id, FVector(Id, 0, 0), Id % 2});
	}
	const FSpatialTarget* OwnedAllocation = Input.GetData();
	TestTrue(TEXT("Full bounded value input builds"), Snapshot.Build(MoveTemp(Input)));
	TestTrue(TEXT("The producer allocation becomes the immutable snapshot without a second target array"), Snapshot.GetTargets().GetData() == OwnedAllocation);
	TestEqual(TEXT("No valid dense input is lost"), Snapshot.GetTargets().Num(), FTargetSpatialSnapshot::MaxEntries);
	for (int32 Id = 0; Id < Snapshot.GetTargets().Num(); ++Id)
	{
		const auto& Value = Snapshot.GetTargets()[Id];
		if (Value.Id != Id || Value.Position != FVector(Id, 0, 0) || Value.Team != Id % 2) { AddError(TEXT("Owned dense input value/order changed")); break; }
	}
	TArray<FSpatialTarget> Mixed = {
		{-1, FVector::ZeroVector, 2}, {9, FVector(-1000, 0, 0), 2},
		{9, FVector::ZeroVector, -1}, {3, FVector::ZeroVector, 2},
		{2, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 2}, {1, FVector(1000, 0, 0), 2}};
	OwnedAllocation = Mixed.GetData();
	TestTrue(TEXT("Invalid input rows are compacted without changing valid id semantics"), Snapshot.Build(MoveTemp(Mixed)));
	TestTrue(TEXT("Filtering retains the producer allocation"), Snapshot.GetTargets().GetData() == OwnedAllocation);
	const auto& Values = Snapshot.GetTargets();
	if (TestEqual(TEXT("Only three valid rows remain"), Values.Num(), 3))
	{
		TestTrue(TEXT("Compaction retains source order"), Values[0].Id == 9 && Values[1].Id == 3 && Values[2].Id == 1);
	}
	TArray<int32> Indices;
	FSpatialQueryStats Stats;
	TestTrue(TEXT("Compacted cells remain queryable"), Snapshot.Query(FVector::ZeroVector, 1000, 1, Indices, Stats));
	if (TestEqual(TEXT("All exact boundary rows returned"), Indices.Num(), 3))
	{
		TestTrue(TEXT("Query tie order still uses stable ids"), Values[Indices[0]].Id == 1 && Values[Indices[1]].Id == 3 && Values[Indices[2]].Id == 9);
	}
	TestFalse(TEXT("Duplicate valid ids fail even after invalid rows were removed"),
		Snapshot.Build({{-1, FVector::ZeroVector, 2}, {7, FVector::ZeroVector, 2}, {7, FVector(1, 0, 0), 2}}));
	TestTrue(TEXT("A duplicate failure publishes no partial rows"), Snapshot.GetTargets().IsEmpty());
	TestTrue(TEXT("A duplicate failure leaves no previous spatial cells"),
		Snapshot.Query(FVector::ZeroVector, 1000, 1, Indices, Stats) && Indices.IsEmpty());
	TestTrue(TEXT("The same snapshot can rebuild after duplicate rejection"), Snapshot.Build({{4, FVector::ZeroVector, 2}}));
	TestTrue(TEXT("Recovery sees only the new row"), Snapshot.Query(FVector::ZeroVector, 1, 1, Indices, Stats) && Indices.Num() == 1 && Snapshot.GetTargets()[Indices[0]].Id == 4);
	TestTrue(TEXT("Interleaved invalid rows compact into one cell"), Snapshot.Build({
		{-1, FVector::ZeroVector, 2}, {9, FVector(90, 0, 0), 2}, {4, FVector::ZeroVector, -1},
		{3, FVector(30, 0, 0), 2}, {2, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 2},
		{1, FVector(10, 0, 0), 2}}));
	Snapshot.Query(FVector::ZeroVector, 100, 1, Indices, Stats);
	if (TestEqual(TEXT("Same-cell traversal visits all compacted rows once"), Indices.Num(), 3))
	{
		TestTrue(TEXT("Compacted same-cell query retains stable id order"), Snapshot.GetTargets()[Indices[0]].Id == 1
			&& Snapshot.GetTargets()[Indices[1]].Id == 3 && Snapshot.GetTargets()[Indices[2]].Id == 9);
	}
	TestEqual(TEXT("Invalid source positions are never retained in the cell"), Stats.CandidatesVisited, 3);
	bool bFound = false;
	Snapshot.HasAny(FVector::ZeroVector, 100, 1, bFound, Stats);
	TestTrue(TEXT("Compacted same-cell presence preserves insertion-order early exit"), bFound && Stats.CandidatesVisited == 1);
	return !HasAnyErrors();
}

// Observational comparison, with no machine-specific timing pass/fail threshold.
// Keep the same inputs before/after changing the production spatial storage.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialStorageBenchmark, "ProjectJ.AsyncTargeting.SpatialStorageBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialStorageBenchmark::RunTest(const FString&)
{
	using namespace ProjectJ::TargetScoring;
	for (int32 Count : {2048, 4096}) for (int32 Layout = 0; Layout < 3; ++Layout)
	{
		TArray<FSpatialTarget> Values;
		for (int32 Id = 0; Id < Count; ++Id)
		{
			// One dense cell, 32 interleaved cells, and one cell per target.
			const int32 Cell = Layout == 0 ? 0 : Layout == 1 ? Id % 32 : Id;
			Values.Add({Count - Id, FVector(Cell * 2000.0 + Id % 100, 10, Id % 7), Id % 3});
		}
		for (bool bReuse : {false, true})
		{
			FTargetSpatialSnapshot Reused;
			TArray<double> BuildTimes, QueryTimes, PresenceTimes;
			int64 Returned = 0, Visited = 0, Matches = 0;
			for (int32 Sample = 0; Sample < 34; ++Sample)
			{
				FTargetSpatialSnapshot Fresh;
				auto& Index = bReuse ? Reused : Fresh;
				auto Input = Values; // Producer collection/copy is outside the build measurement.
				double Started = FPlatformTime::Seconds();
				const bool bBuilt = Index.Build(MoveTemp(Input));
				const double BuildTime = (FPlatformTime::Seconds() - Started) * 1.e6;
				if (!bBuilt) { AddError(TEXT("Spatial storage comparison failed to build valid bounded input")); return false; }
				TArray<int32> Indices;
				Started = FPlatformTime::Seconds();
				for (int32 Query = 0; Query < 32; ++Query)
				{
					FSpatialQueryStats Stats;
					Index.Query(Values[(Query * 67) % Count].Position, 2500, 1, Indices, Stats);
					if (Sample >= 2) { Returned += Indices.Num(); Visited += Stats.CandidatesVisited; }
				}
				const double QueryTime = (FPlatformTime::Seconds() - Started) * 1.e6;
				Started = FPlatformTime::Seconds();
				for (int32 Query = 0; Query < 32; ++Query)
				{
					FSpatialQueryStats Stats; bool bFound = false;
					Index.HasAny(Values[(Query * 67) % Count].Position, 2500, 1, bFound, Stats);
					if (Sample >= 2) { Matches += bFound ? 1 : 0; }
				}
				const double PresenceTime = (FPlatformTime::Seconds() - Started) * 1.e6;
				if (Sample >= 2) { BuildTimes.Add(BuildTime); QueryTimes.Add(QueryTime); PresenceTimes.Add(PresenceTime); }
			}
			BuildTimes.Sort(); QueryTimes.Sort(); PresenceTimes.Sort();
			AddInfo(FString::Printf(TEXT("SpatialStorage targets=%d layout=%d reuse=%d samples=32 build_us_p50=%.3f build_us_p95=%.3f query32_us_p50=%.3f presence32_us_p50=%.3f returned=%lld visited=%lld matches=%lld"),
				Count, Layout, bReuse ? 1 : 0, BuildTimes[16], BuildTimes[30], QueryTimes[16], PresenceTimes[16], Returned / 32, Visited / 32, Matches / 32));
		}
	}
	return true;
}

#endif
