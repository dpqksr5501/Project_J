#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "HAL/PlatformTime.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialPresenceTest, "ProjectJ.AsyncTargeting.SpatialPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialPresenceTest::RunTest(const FString&)
{
	using namespace ProjectJ::TargetScoring;
	FRandomStream Random(20261026);
	TArray<FSpatialTarget> Values;
	for (int32 I = 0; I < 512; ++I) Values.Add({I, FVector(Random.FRandRange(-100000, 100000), Random.FRandRange(-100000, 100000), Random.FRandRange(-20000, 20000)), I % 3});
	Values.Add({512, FVector(-5000, 5000, 10000), 1});
	Values.Add({513, FVector(3.e15, -3.e15, 500), 1});
	FTargetSpatialSnapshot Index;
	TestTrue(TEXT("Presence snapshot builds"), Index.Build(Values, 5000));
	for (int32 I = 0; I < 300; ++I)
	{
		const FVector Origin = I == 0 ? Values.Last().Position : FVector(Random.FRandRange(-100000, 100000), Random.FRandRange(-100000, 100000), Random.FRandRange(-20000, 20000));
		const double Radius = I % 5 == 0 ? 1.e8 : I % 5 == 1 ? 0 : Random.FRandRange(1, 11000);
		bool Found = false;
		FSpatialQueryStats Stats, FullStats;
		TArray<int32> Full;
		TestTrue(TEXT("Exact presence query accepted"), Index.HasAny(Origin, Radius, I % 3, Found, Stats));
		Index.Query(Origin, Radius, I % 3, Full, FullStats);
		TestEqual(TEXT("Presence equals existing exact 3D query including excluded/extreme/empty ranges"), Found, !Full.IsEmpty());
		TestTrue(TEXT("Each value visited at most once"), Stats.CandidatesVisited <= Values.Num());
	}
	bool Found = true; FSpatialQueryStats Stats;
	TestFalse(TEXT("NaN origin rejected"), Index.HasAny(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 1, 0, Found, Stats));
	TestFalse(TEXT("Rejected query clears previous presence"), Found);
	TestFalse(TEXT("Negative radius rejected"), Index.HasAny(FVector::ZeroVector, -1, 0, Found, Stats));
	TestFalse(TEXT("Infinite radius rejected"), Index.HasAny(FVector::ZeroVector, std::numeric_limits<double>::infinity(), 0, Found, Stats));
	Index.Build({});
	TestTrue(TEXT("Empty snapshot is valid"), Index.HasAny(FVector::ZeroVector, 10, 0, Found, Stats));
	TestFalse(TEXT("Empty snapshot has no match"), Found);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialPresenceBenchmark, "ProjectJ.AsyncTargeting.SpatialPresenceBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSpatialPresenceBenchmark::RunTest(const FString&)
{
	using namespace ProjectJ::TargetScoring;
	for (int32 Count : {512, 4096}) for (int32 Layout = 0; Layout < 3; ++Layout)
	{
		TArray<FSpatialTarget> Values;
		for (int32 I = 0; I < Count; ++I)
		{
			const double Spacing = Layout == 1 ? 200000.0 : 20.0;
			Values.Add({I, FVector((I % 64) * Spacing, (I / 64) * Spacing, (I % 4) * 25), 1});
		}
		FTargetSpatialSnapshot Index; Index.Build(Values, 100000);
		TArray<double> FullTimes, PresenceTimes;
		int64 PresenceVisits = 0, FullVisits = 0;
		for (int32 Sample = 0; Sample < 34; ++Sample)
		{
			int32 Matches[2] = {}; double Times[2] = {};
			for (int32 Step = 0; Step < 2; ++Step)
			{
				const int32 Mode = (Sample + Step) % 2;
				const double Started = FPlatformTime::Seconds();
				TArray<int32> Full; // Reuse capacity even though the original activation path did not.
				for (int32 Q = 0; Q < 128; ++Q)
				{
					const FVector Origin = Layout == 2 ? FVector(-1.e8, 1.e8, 1.e8) : Values[(Q * 4) % Count].Position + FVector(7, 11, 13);
					FSpatialQueryStats Stats; bool Found = false;
					if (Mode) Index.HasAny(Origin, 150000, 0, Found, Stats);
					else { Index.Query(Origin, 150000, 0, Full, Stats); Found = !Full.IsEmpty(); }
					Matches[Mode] += Found ? 1 : 0;
					if (Sample >= 2) { if (Mode) PresenceVisits += Stats.CandidatesVisited; else FullVisits += Stats.CandidatesVisited; }
				}
				Times[Mode] = (FPlatformTime::Seconds() - Started) * 1.e6;
			}
			TestEqual(TEXT("Both activation paths match"), Matches[0], Matches[1]);
			if (Sample >= 2) { FullTimes.Add(Times[0]); PresenceTimes.Add(Times[1]); }
		}
		FullTimes.Sort(); PresenceTimes.Sort();
		AddInfo(FString::Printf(TEXT("SpatialPresence layout=%d observers=%d queries=128 samples=32 full_query_us_p50=%.3f presence_us_p50=%.3f full_visits=%lld presence_visits=%lld"), Layout, Count, FullTimes[16], PresenceTimes[16], FullVisits / 32, PresenceVisits / 32));
	}
	return true;
}
#endif
