#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JAnimNode_FootClearance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJFootClearanceTest,"ProjectJ.Animation.FootClearance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJFootClearanceTest::RunTest(const FString&)
{
	const FPlane Floor(FVector::ZeroVector,FVector::UpVector);
	TArray<FVector> Points={{0,0,3},{2,0,-4},{-2,0,-1}};
	TestEqual(TEXT("Lowest sole point determines minimal lift"),FProject_JAnimNode_FootClearance::RequiredLift(Points,Floor,.15f,10.f),4.15f);
	for(auto& Point:Points)Point.Z+=4.15;
	TestTrue(TEXT("A corrected sole has no repeated offset accumulation"),FProject_JAnimNode_FootClearance::RequiredLift(Points,Floor,.15f,10.f)<.0001f);
	for(auto& Point:Points)Point.Z+=50;
	TestEqual(TEXT("Swinging feet above the floor are preserved"),FProject_JAnimNode_FootClearance::RequiredLift(Points,Floor,.15f,10.f),0.f);
	Points={{0,0,-30}};
	TestEqual(TEXT("Exceptional poses cannot move the leg without a bound"),FProject_JAnimNode_FootClearance::RequiredLift(Points,Floor,.15f,10.f),10.f);
	const FVector Normal=FVector(0,1,1).GetSafeNormal();const FPlane Slope(FVector(0,0,10),Normal);
	Points={FVector(0,0,10)-Normal*2};
	TestTrue(TEXT("Contact clearance is normal to the traced slope"),FMath::IsNearlyEqual(FProject_JAnimNode_FootClearance::RequiredLift(Points,Slope,.15f,10.f),2.15f,.0001f));
	return true;
}
#endif
