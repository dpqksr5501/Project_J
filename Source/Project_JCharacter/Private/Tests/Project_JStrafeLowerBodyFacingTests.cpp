#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JStrafeLowerBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLowerBodyFacingTest, "ProjectJ.StrafeLowerBodyFacing.OwnershipAndFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLowerBodyFacingTest::RunTest(const FString&)
{
	using namespace Project_J::Animation;
	FProject_JAnimThreadSafeData D;
	FProject_JStrafeLowerBodyFacingPolicy P;
	D.Combat.bIsCombatMode = true;
	D.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	D.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	D.LocomotionContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
	D.LocomotionContext.RequestedMoveWorldDirection = FVector::ForwardVector;
	D.Movement.Velocity = FVector(300, 0, 0); D.Movement.GroundSpeed = 300;
	D.Input.bHasMoveInput = true; D.Aim.AimOffsetAlpha = 1;
	D.ProceduralIK.FootPlacementAlpha = D.ProceduralIK.LegIKAlpha = 1;
	const auto Baseline = D;
	FVector2D LocalInput(0, 1);
	auto Build = [&]() { return BuildStrafeLowerBodyFacingInput(D, P, true, false, true, 0, LocalInput); };
	TestTrue(TEXT("Local forward Run can correct"), Build().Facing.bEnabled);
	for (auto Phase : {EProject_JLocomotionPhaseFamily::Idle, EProject_JLocomotionPhaseFamily::Turn,
		EProject_JLocomotionPhaseFamily::TurnInPlace, EProject_JLocomotionPhaseFamily::Start, EProject_JLocomotionPhaseFamily::Landing})
	{
		D = Baseline; D.LocomotionContext.PhaseFamily = Phase;
		TestFalse(TEXT("Idle, every Turn and one-shot have no residual correction"), Build().Facing.bCanRelease);
	}
	D = Baseline; D.TurnRequest.bAcuteActive = true;
	TestFalse(TEXT("Turn owner still in Cycle is excluded immediately"), Build().Facing.bCanRelease);
	D = Baseline; D.OneShotPresentation.bEnabled = true;
	D.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TurnInPlace;
	TestFalse(TEXT("Presentation TIP cannot leak through a Cycle context"), Build().Facing.bCanRelease);
	D.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TransitionToLocomotion;
	TestFalse(TEXT("Start handoff presentation is excluded"), Build().Facing.bCanRelease);
	D = Baseline; D.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation = true;
	TestFalse(TEXT("Continuing Turn owns entire lower pose"), Build().Facing.bCanRelease);
	D = Baseline; D.Combat.bIsAttacking = true;
	TestFalse(TEXT("Attack bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.Combat.bIsDodging = true;
	TestFalse(TEXT("Dodge bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.Combat.bIsHitReacting = true;
	TestFalse(TEXT("Hit reaction bypass"), Build().Facing.bCanRelease);
	D = Baseline;
	TestFalse(TEXT("Montage/root-motion owner bypass"), BuildStrafeLowerBodyFacingInput(D, P, true, true, true, 0, LocalInput).Facing.bCanRelease);
	TestFalse(TEXT("Remote/follower bypass"), BuildStrafeLowerBodyFacingInput(D, P, false, false, true, 0, LocalInput).Facing.bCanRelease);
	TestFalse(TEXT("Console off bypass"), BuildStrafeLowerBodyFacingInput(D, P, true, false, false, 0, LocalInput).Facing.bCanRelease);
	D.Air.bIsInAir = true; TestFalse(TEXT("Air bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	TestFalse(TEXT("OTM bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.Input.bHasMoveInput = false;
	TestFalse(TEXT("Stationary rotation bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.ProceduralIK.LegIKAlpha = .5;
	TestFalse(TEXT("Authored lower-body/disabled IK policy bypass"), Build().Facing.bCanRelease);
	D = Baseline; D.LocomotionContext.GaitIntent = EProject_JLocomotionGaitIntent::Sprint;
	TestFalse(TEXT("Sprint is outside this Run fix"), Build().Facing.bCanRelease);
	D = Baseline; D.LocomotionContext.RequestedMoveWorldDirection = FVector::RightVector;
	TestFalse(TEXT("Side input requests no new yaw"), Build().Facing.bEnabled);
	TestTrue(TEXT("Side Cycle can release residual yaw"), Build().Facing.bCanRelease);
	D = Baseline;
	for (const FVector2D Side : {FVector2D(-1, 0), FVector2D(0, -1), FVector2D(1, 0)})
	{
		LocalInput = Side;
		TestFalse(TEXT("Camera/capsule lag cannot reinterpret actual A/S/D as forward"), Build().Facing.bEnabled);
	}
	LocalInput = FVector2D::ZeroVector;
	TestFalse(TEXT("No held local input is exact bypass even while coasting"), Build().Facing.bCanRelease);
	LocalInput = FVector2D(0, 1);
	D = Baseline; P.MaxCorrection = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Malformed policy uses bounded fallback"), FMath::IsFinite(Build().Facing.Policy.MaxCorrection));
	// A bent leg is tested with both yaw signs and a tilted pelvis. The target
	// includes foot orientation and non-unit uniform scale, not just a position.
	for (const float Yaw : {-25.f, 25.f})
	{
		const FTransform OriginalPelvis(FRotator(5, 0, 3), FVector(0, 0, 90), FVector(1.1));
		const FTransform Thigh(FQuat::Identity, FVector(0, 8, 0));
		const FTransform Calf(FQuat::Identity, FVector(12, 0, -38));
		const FTransform Foot(FRotator(0, 15, 0), FVector(-12, 0, -38));
		const FTransform Target = Foot * (Calf * (Thigh * OriginalPelvis));
		FTransform Pelvis = OriginalPelvis;
		Pelvis.SetRotation((FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)) * Pelvis.GetRotation()).GetNormalized());
		FTransform A, B, C;
		TestTrue(TEXT("Bent leg can face while retaining foot"), SolveFacingLeg(Pelvis, Thigh, Calf, Foot, Target, A, B, C));
		TestTrue(TEXT("Entire evaluated foot transform is preserved"), C.Equals(Target, .0001));
		const FTransform OriginalA = Thigh * OriginalPelvis, OriginalB = Calf * OriginalA;
		TestTrue(TEXT("Upper limb length is unchanged"), FMath::IsNearlyEqual(FVector::Distance(A.GetLocation(), B.GetLocation()), FVector::Distance(OriginalA.GetLocation(), OriginalB.GetLocation()), .001));
		TestTrue(TEXT("Lower limb length is unchanged"), FMath::IsNearlyEqual(FVector::Distance(B.GetLocation(), C.GetLocation()), FVector::Distance(OriginalB.GetLocation(), Target.GetLocation()), .001));
		FTransform TooFar = Target; TooFar.AddToTranslation(FVector(1000, 0, 0));
		TestFalse(TEXT("Unreachable contact refuses leg stretch"), SolveFacingLeg(Pelvis, Thigh, Calf, Foot, TooFar, A, B, C));
	}
	return true;
}
#endif
