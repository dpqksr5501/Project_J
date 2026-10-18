#include "Misc/AutomationTest.h"
#include "World/Project_JWorldState.h"
#include "World/Project_JWorldStreamingComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEnvironmentTransitionTest,"ProjectJ.World.EnvironmentTransition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJEnvironmentTransitionTest::RunTest(const FString&)
{
    FProject_JEnvironmentState State;
    State.FromRain=.2f; State.ToRain=1; State.TransitionStart=100; State.TransitionSeconds=20;
    TestEqual(TEXT("Before start remains old state"), AProject_JWorldState::EvaluateRain(State,90),.2f);
    TestTrue(TEXT("Late join reconstructs midpoint"),FMath::IsNearlyEqual(AProject_JWorldState::EvaluateRain(State,110),.6f));
    TestEqual(TEXT("After completion clamps target"),AProject_JWorldState::EvaluateRain(State,150),1.f);
    State.TransitionSeconds=0;
    TestEqual(TEXT("Instant transitions do not divide by zero"),AProject_JWorldState::EvaluateRain(State,100),1.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStreamingPredictionTest,"ProjectJ.World.StreamingPrediction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJStreamingPredictionTest::RunTest(const FString&)
{
    const FVector Up = UProject_JWorldStreamingComponent::PredictOffset(FVector(0,0,-10000),3,20000);
    TestEqual(TEXT("Fast descent prepares below pawn, bounded by budget"),Up,FVector(0,0,-20000));
    TestEqual(TEXT("Stopped pawn keeps nearby preparation"),UProject_JWorldStreamingComponent::PredictOffset(FVector::ZeroVector,3,60000),FVector::ZeroVector);
    const FVector Turn = UProject_JWorldStreamingComponent::PredictOffset(FVector(-4000,0,0),3,60000);
    TestEqual(TEXT("Reversal follows actual movement"),Turn,FVector(-12000,0,0));
    return true;
}
#endif
