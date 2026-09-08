#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
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
#endif
