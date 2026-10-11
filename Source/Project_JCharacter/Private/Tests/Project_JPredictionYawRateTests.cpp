#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPredictionYawRateFilter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPredictionYawRateTest, "ProjectJ.Animation.PredictionYawRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPredictionYawRateTest::RunTest(const FString&)
{
	for (float Sign : {-1.f, 1.f})
	{
		FProject_JPredictionYawRateFilter Filter;
		float LastRate = 0;
		for (uint64 Frame = 0; Frame < 240; ++Frame)
		{
			const float Raw = Frame % 3 ? 0 : Sign * 270;
			LastRate = Filter.Update(Raw, Frame / 120., Frame, .025f, true);
			if (Frame > 120) TestTrue(TEXT("A packet-delivered 90deg/s curve has stable clamped prediction"), Sign * LastRate >= 69.9f);
			TestEqual(TEXT("Same-frame rebuild does not erase the first sample"), Filter.Update(0, Frame / 120., Frame, .025f, true), LastRate);
		}
		for (uint64 Frame = 240; Frame < 264; ++Frame) LastRate = Filter.Update(0, Frame / 120., Frame, .025f, true);
		TestTrue(TEXT("Stopped input leaves negligible prediction after eight half-lives"), FMath::Abs(LastRate) < 1);
		TestEqual(TEXT("Large physical facing error, action, air or ownership loss bypasses smoothing immediately"), Filter.Update(Sign * 1000, 2.3, 300, .025f, false), Sign * 1000);
		TestEqual(TEXT("A stale prediction cannot carry old angular velocity"), Filter.Update(0, 3., 360, .025f, true), 0.f);
		TestEqual(TEXT("Clock reversal starts a fresh sample"), Filter.Update(40, 1., 361, .025f, true), 40.f);
		Filter.Reset();
		TestEqual(TEXT("Trajectory reset discards rate history"), Filter.Update(0, 4., 500, .025f, true), 0.f);
		TestEqual(TEXT("Zero half-life is the legacy path"), Filter.Update(80, 4.01, 501, 0, true), 80.f);
	}
	return true;
}
#endif
