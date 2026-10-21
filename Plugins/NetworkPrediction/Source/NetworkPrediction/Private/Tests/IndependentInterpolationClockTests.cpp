// Project J regression for the UE 5.8.3 independent presentation clock patch.
#if WITH_DEV_AUTOMATION_TESTS
#include "IndependentInterpolationClock.h"
#include "Misc/AutomationTest.h"

using namespace UE::Net::Private;
constexpr auto ClockFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndependentClockLateJoin,"ProjectJ.NetworkPrediction.ClockLateJoin",ClockFlags)
bool FIndependentClockLateJoin::RunTest(const FString&)
{
	float Time=0.f;
	TestTrue(TEXT("Wait for an actual buffered stream"),AdvanceIndependentInterpolationClock(Time,80,100,250,16)==EIndependentClockResult::Waiting);
	TestEqual(TEXT("No fabricated timestamp while waiting"),Time,0.f);
	TestTrue(TEXT("Late join is initialization, not a behind fault"),AdvanceIndependentInterpolationClock(Time,489,100,250,8)==EIndependentClockResult::Initialized);
	TestTrue(TEXT("Starts in the received epoch with the configured buffer"),FMath::IsNearlyEqual(Time,397.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndependentClockHitch,"ProjectJ.NetworkPrediction.ClockHitchRecovery",ClockFlags)
bool FIndependentClockHitch::RunTest(const FString&)
{
	float Time=900.f;
	AdvanceIndependentInterpolationClock(Time,1000,100,250,275.f);
	TestTrue(TEXT("275ms hitch cannot consume future state"),Time<1000.f);
	const float Held=Time;
	for(int32 I=0;I<20;++I) AdvanceIndependentInterpolationClock(Time,1000,100,250,16.f);
	TestEqual(TEXT("Missing packets hold a valid state without extrapolation or rewind"),Time,Held);
	for(int32 I=1;I<=180;++I)
	{
		const float Previous=Time;
		const int32 Latest=1000+I*16;
		AdvanceIndependentInterpolationClock(Time,Latest,100,250,16.f);
		if(!TestTrue(TEXT("Recovered presentation stays monotonic and inside reception"),Time>=Previous && Time<Latest)) return false;
	}
	TestTrue(TEXT("The original 100ms target recovers without increasing latency"),3880.f-Time>75.f && 3880.f-Time<101.f);
	float Invalid=2000.f;
	TestTrue(TEXT("Genuine invalid timestamps remain diagnosable"),AdvanceIndependentInterpolationClock(Invalid,1500,100,250,16)==EIndependentClockResult::InvalidTime);
	TestEqual(TEXT("Invalid state is not silently reset"),Invalid,2000.f);
	return true;
}
#endif
