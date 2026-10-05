#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JAnimationFlowTrace.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationFlowSamplingTest,
	"ProjectJ.Animation.FlowTrace.Sampling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationFlowSamplingTest::RunTest(const FString&)
{
	FProject_JAnimationFlowSampler Sampler;
	FProject_JAnimationFlowKey Key;
	bool bEdge = false;
	TestFalse(TEXT("Default-off trace emits nothing"), Sampler.ShouldRecord(Key, 0, 0, 0, false, bEdge));
	TestTrue(TEXT("Enabling records a baseline"), Sampler.ShouldRecord(Key, 0, 0, 1, false, bEdge));
	TestFalse(TEXT("Unchanged frames are throttled"), Sampler.ShouldRecord(Key, 0, 0.01, 1, false, bEdge));
	Key.SelectionRevision++;
	TestTrue(TEXT("A replacement within the sampling interval is retained"), Sampler.ShouldRecord(Key, 0, 0.02, 1, false, bEdge));
	TestTrue(TEXT("Replacement is labelled as an edge"), bEdge);
	TestTrue(TEXT("Post-transition context is sampled"), Sampler.ShouldRecord(Key, 0, 0.2, 1, false, bEdge));
	TestFalse(TEXT("Settled periodic logging expires"), Sampler.ShouldRecord(Key, 0, 1.1, 1, false, bEdge));
	TestTrue(TEXT("An ongoing one-shot keeps its sampling window"), Sampler.ShouldRecord(Key, 0, 2, 1, true, bEdge));
	Sampler.ShouldRecord(Key, 1, 2.01, 1, true, bEdge);
	Sampler.ShouldRecord(Key, 2, 2.02, 1, true, bEdge);
	TestTrue(TEXT("Small yaw changes accumulate into a camera edge"), Sampler.ShouldRecord(Key, 3, 2.03, 1, true, bEdge));
	TestTrue(TEXT("Camera accumulation is labelled as an edge"), bEdge);
	Sampler.ShouldRecord(Key, 179, 3, 1, false, bEdge);
	TestFalse(TEXT("Yaw wrapping does not create a false large-turn edge"), Sampler.ShouldRecord(Key, -179, 3.01, 1, false, bEdge));
	TestTrue(TEXT("Mode two samples settled locomotion"), Sampler.ShouldRecord(Key, 179, 5, 2, false, bEdge));
	TestTrue(TEXT("World time reset starts a new baseline"), Sampler.ShouldRecord(Key, 179, 0, 1, false, bEdge));
	TestTrue(TEXT("Reset baseline is an edge"), bEdge);
	Sampler.ShouldRecord(Key, 179, 0.01, 0, false, bEdge);
	TestTrue(TEXT("Re-enabling starts a fresh session"), Sampler.ShouldRecord(Key, 179, 0.02, 1, false, bEdge));
	TestTrue(TEXT("Re-enabled baseline is an edge"), bEdge);
	return true;
}
#endif
