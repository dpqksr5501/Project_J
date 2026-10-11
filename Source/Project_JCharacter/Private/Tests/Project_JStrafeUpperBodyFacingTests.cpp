#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUpperBodyFacingTest, "ProjectJ.StrafeUpperBodyFacing.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUpperBodyFacingTest::RunTest(const FString&)
{
	using namespace Project_J::Animation;
	FProject_JAnimThreadSafeData Data;
	FProject_JStrafeUpperBodyFacingPolicy Policy;
	Data.Combat.bIsCombatMode = true;
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;
	Data.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.LocomotionContext.RequestedMoveWorldDirection = FVector::ForwardVector;
	Data.Input.bHasMoveInput = true;
	Data.Movement.GroundSpeed = 300.f;
	Data.Aim.AimOffsetAlpha = 1.f;
	auto Build = [&]() { return BuildStrafeUpperBodyFacingInput(Data, Policy, true, false, true, 0.f); };
	TestTrue(TEXT("Forward local Strafe cycle owns presentation"), Build().bEnabled);
	for (const FVector Direction : {FVector::RightVector, -FVector::RightVector, -FVector::ForwardVector})
	{
		Data.LocomotionContext.RequestedMoveWorldDirection = Direction;
		const auto Input = Build();
		TestFalse(TEXT("Side/back input does not request new torso correction"), Input.bEnabled);
		TestTrue(TEXT("Side/back input can release the previous correction"), Input.bCanRelease);
	}
	Data.LocomotionContext.RequestedMoveWorldDirection = FVector::ForwardVector;
	TestFalse(TEXT("Remote/follower bypass"), BuildStrafeUpperBodyFacingInput(Data, Policy, false, false, true, 0.f).bCanRelease);
	TestFalse(TEXT("Root-motion/montage owner bypass"), BuildStrafeUpperBodyFacingInput(Data, Policy, true, true, true, 0.f).bCanRelease);
	TestFalse(TEXT("Console off is exact bypass"), BuildStrafeUpperBodyFacingInput(Data, Policy, true, false, false, 0.f).bCanRelease);
	Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
	TestFalse(TEXT("One-shot owns its authored torso"), Build().bCanRelease);
	Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	Data.TurnRequest.bAcuteActive = true;
	TestFalse(TEXT("Acute turn never requests new correction"), Build().bEnabled);
	TestTrue(TEXT("Acute turn releases previous correction without a pop"), Build().bCanRelease);
	Data.TurnRequest.bAcuteActive = false;
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = true;
	TestFalse(TEXT("General turn never requests new correction"), Build().bEnabled);
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = false;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	TestFalse(TEXT("Turn phase without request still releases instead of correcting"), Build().bEnabled);
	TestTrue(TEXT("Turn phase keeps bounded release active"), Build().bCanRelease);
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	TestFalse(TEXT("OTM is exact source bypass"), Build().bCanRelease);
	Data.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Data.Air.bIsInAir = true;
	TestFalse(TEXT("Air bypass"), Build().bCanRelease);
	Data.Air.bIsInAir = false;
	Data.Aim.AimOffsetAlpha = 0.f;
	TestFalse(TEXT("Inactive aim bypass"), Build().bCanRelease);
	Data.Aim.AimOffsetAlpha = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Invalid target bypass"), Build().bCanRelease);
	TestEqual(TEXT("Small authored yaw is preserved"), UpperBodyFacingDemand(5.f, 1.f, Policy), 0.f);
	TestEqual(TEXT("Only excess yaw is removed"), UpperBodyFacingDemand(40.f, 1.f, Policy), 32.f);
	TestEqual(TEXT("Left/right correction is symmetric"), UpperBodyFacingDemand(-40.f, 1.f, Policy), -32.f);
	TestEqual(TEXT("Correction is capped"), UpperBodyFacingDemand(150.f, 1.f, Policy), 40.f);
	TestEqual(TEXT("Root aligned keeps correction"), UpperBodyRootFacingWeight(25.f, Policy), 1.f);
	TestEqual(TEXT("Root catching up yields to authored turn"), UpperBodyRootFacingWeight(45.f, Policy), 0.f);
	TestEqual(TEXT("Root fading is symmetric"), UpperBodyRootFacingWeight(-35.f, Policy), UpperBodyRootFacingWeight(35.f, Policy));
	float Final30 = 0.f;
	for (const int32 FPS : {30, 60, 120})
	{
		float Current = 0.f;
		for (int32 Frame = 0; Frame < FPS; ++Frame)
		{
			const float Next = StepUpperBodyFacing(Current, 32.f, 1.f / FPS, Policy);
			TestTrue(TEXT("Correction rate bounded at each FPS"), FMath::Abs(Next - Current) <= Policy.MaxCorrectionRate / FPS + .001f);
			Current = Next;
		}
		if (FPS == 30) Final30 = Current;
		TestTrue(TEXT("Convergence is independent of frame partition"), FMath::Abs(Current - Final30) < .001f);
		for (int32 Frame = 0; Frame < FPS; ++Frame) Current = StepUpperBodyFacing(Current, 0.f, 1.f / FPS, Policy);
		TestTrue(TEXT("Released correction converges to exact source"), FMath::Abs(Current) < .001f);
	}
	TestTrue(TEXT("Large frame stall cannot exceed bounded 100ms step"), StepUpperBodyFacing(0.f, 40.f, 5.f, Policy) <= 36.f);
	const float FirstRelease = StepUpperBodyFacing(40.f, 0.f, 1.f/60.f, Policy);
	TestTrue(TEXT("Turn entry cannot remove 40 degrees in one frame"), FirstRelease >= 34.f && FirstRelease < 40.f);
	return true;
}
#endif
