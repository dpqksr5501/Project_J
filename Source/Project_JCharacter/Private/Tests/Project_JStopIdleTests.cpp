#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStopIdleInterruptTest, "ProjectJ.Animation.StopIdleInterrupt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStopIdleInterruptTest::RunTest(const FString&)
{
	FProject_JCharacterAnimInstanceProxy Proxy;
	// Both Stop and Idle have false movement intent. The old policy misses this
	// database boundary because neither movement, gait nor stance changes.
	for (bool Combat : {false, true})
	{
		Proxy.ThreadSafeData.Combat.bIsCombatMode = Combat;
		Proxy.ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving = false;
		Proxy.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
		Proxy.CacheMotionMatchingPolicyState();
		Proxy.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		TestEqual(TEXT("Stop to Idle rejects the previous database's continuing pose"),
			Proxy.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		// A throttled chooser can apply the database after the semantic edge.
		Proxy.CacheMotionMatchingPolicyState();
		TestEqual(TEXT("Delayed Idle database application retains the same contract"),
			Proxy.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
	}
	Proxy.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Proxy.ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving = true;
	Proxy.CacheMotionMatchingPolicyState();
	TestEqual(TEXT("Sustained locomotion keeps its continuing pose"), Proxy.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	Proxy.ThreadSafeData.Air.bIsInAir = true;
	Proxy.CacheMotionMatchingPolicyState();
	TestEqual(TEXT("Sustained air playback remains uninterrupted"), Proxy.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	return true;
}
#endif
