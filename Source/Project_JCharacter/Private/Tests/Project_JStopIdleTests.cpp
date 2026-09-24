#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/SkeletalMeshComponent.h"

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
	for (bool Combat : {false, true})
	{
		FProject_JCharacterAnimInstanceProxy MontageProxy;
		FProject_JAnimThreadSafeData MovingData;
		MovingData.Combat.bIsCombatMode = Combat;
		MovingData.Combat.bIsPlayingCombatIntro = Combat;
		MovingData.Combat.bIsPlayingCombatOutro = !Combat;
		MovingData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		MovingData.LocomotionContext.bIsMotionMatchingMoving = true;
		MontageProxy.QueueGameThreadData(MovingData, nullptr, true, true, false);
		FProject_JAnimThreadSafeData IdleData = MovingData;
		IdleData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		IdleData.LocomotionContext.bIsMotionMatchingMoving = false;
		MontageProxy.QueueGameThreadData(IdleData, nullptr, true, true, false);
		TestTrue(TEXT("Releasing movement under a combat transition invalidates the continuing walk pose"),
			MontageProxy.bForceMotionMatchingReselect);
		MontageProxy.QueueGameThreadData(IdleData, nullptr, true, true, false);
		TestFalse(TEXT("Settled Idle does not force a new search every frame"),
			MontageProxy.bForceMotionMatchingReselect);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnInPlaceAndCombatStopTest, "ProjectJ.Animation.TurnInPlaceAndCombatStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnInPlaceAndCombatStopTest::RunTest(const FString&)
{
	UProject_JLocomotionAnimStateComponent* Locomotion = NewObject<UProject_JLocomotionAnimStateComponent>();
	FProject_JLocomotionKinematicContext Kinematics;
	Kinematics.GroundSpeed = 200.0f;
	Kinematics.bHasFutureTrajectoryVelocity = true;
	Kinematics.FutureTrajectorySpeed = 180.0f;
	Locomotion->bUsingLocalInputState = true;
	TestFalse(TEXT("Released local input wins over a stale moving trajectory"),
		Locomotion->IsMotionMatchingMovingForContext(Kinematics));
	Kinematics.bHasMoveInput = true;
	TestTrue(TEXT("Held local movement continues to locomote"),
		Locomotion->IsMotionMatchingMovingForContext(Kinematics));
	Kinematics.bHasMoveInput = false;
	Locomotion->bUsingLocalInputState = false;
	TestTrue(TEXT("Remote proxy may follow its still moving trajectory"),
		Locomotion->IsMotionMatchingMovingForContext(Kinematics));

	Locomotion->bUsingLocalInputState = true;
	Locomotion->GroundMotionMode = EProject_JGroundMotionMode::Stop;
	Locomotion->GroundSpeed = 150.0f;
	Locomotion->bHasMoveInput = false;
	Locomotion->KinematicContext.bIsDecelerating = false;
	Locomotion->UpdateStopGroundMotionMode();
	TestEqual(TEXT("A noisy local braking sample keeps Stop"),
		Locomotion->GroundMotionMode, EProject_JGroundMotionMode::Stop);
	Locomotion->GroundSpeed = 10.0f;
	Locomotion->UpdateStopGroundMotionMode();
	TestEqual(TEXT("Local Stop reaches Idle at low speed"),
		Locomotion->GroundMotionMode, EProject_JGroundMotionMode::Idle);

	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UProject_JCharacterAnimInstance* Anim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	FProject_JAnimThreadSafeData Data;
	FProject_JAnimOneShotPresentationThreadSafeData OneShot;
	OneShot.bEnabled = true;
	OneShot.bRequested = true;
	OneShot.PhaseFamily = EProject_JLocomotionPhaseFamily::TurnInPlace;
	Data.LocomotionContext.bShouldTurnInPlace = true;
	Data.LocomotionContext.TurnInPlaceDirectionBucket = 3;
	Data.LocomotionContext.TurnInPlaceSequence = 1;
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
	TestEqual(TEXT("First TIP sequence enters its one-shot"),
		OneShot.PresentationState, EProject_JStateControllerPresentationState::TurnInPlace);
	TestEqual(TEXT("Entering TIP consumes its sequence edge"), Anim->StateControllerRuntime.LastHandledRemoteTurnSequence, 1);
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
	TestEqual(TEXT("Completed TIP leaves the one-shot"),
		OneShot.PresentationState, EProject_JStateControllerPresentationState::IdleLoop);
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
	TestEqual(TEXT("Residual yaw cannot replay the consumed TIP sequence"),
		OneShot.PresentationState, EProject_JStateControllerPresentationState::IdleLoop);
	Data.LocomotionContext.TurnInPlaceSequence = 2;
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
	TestEqual(TEXT("A fresh TIP sequence can play"),
		OneShot.PresentationState, EProject_JStateControllerPresentationState::TurnInPlace);

	UProject_JCharacterAnimInstance* UpperBodyAnim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	UpperBodyAnim->StateControllerRuntime.PlaybackHoldState = EProject_JStateControllerPresentationState::LocomotionLoop;
	FProject_JAnimThreadSafeData UpperBodyData;
	UpperBodyData.Combat.bIsPlayingCombatIntro = true;
	UpperBodyData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	UpperBodyData.LocomotionContext.bIsMotionMatchingMoving = false;
	FProject_JAnimOneShotPresentationThreadSafeData UpperBodyOneShot;
	UpperBodyOneShot.bEnabled = true;
	UpperBodyAnim->ResolveStateControllerPresentationStateWithPlaybackHold(UpperBodyData, UpperBodyOneShot);
	TestEqual(TEXT("Upper-body combat draw allows the normal Stop transition"),
		UpperBodyOneShot.PresentationState, EProject_JStateControllerPresentationState::TransitionToIdle);

	UProject_JCharacterAnimInstance* FullBodyAnim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	FullBodyAnim->StateControllerRuntime.PlaybackHoldState = EProject_JStateControllerPresentationState::LocomotionLoop;
	FProject_JAnimThreadSafeData FullBodyData = UpperBodyData;
	FullBodyData.ProceduralIK.FullBodyMontageWeight = 1.0f;
	FProject_JAnimOneShotPresentationThreadSafeData FullBodyOneShot = UpperBodyOneShot;
	FullBodyAnim->ResolveStateControllerPresentationStateWithPlaybackHold(FullBodyData, FullBodyOneShot);
	TestEqual(TEXT("Full-body combat draw defers the Stop transition"),
		FullBodyOneShot.PresentationState, EProject_JStateControllerPresentationState::IdleLoop);

	UProject_JCharacterAnimInstance* StopAnim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	StopAnim->StateControllerRuntime.PlaybackHoldState = EProject_JStateControllerPresentationState::LocomotionLoop;
	FProject_JAnimThreadSafeData StopData;
	StopData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	FProject_JAnimOneShotPresentationThreadSafeData StopOneShot;
	StopOneShot.bEnabled = true;
	StopOneShot.bRequested = true;
	StopOneShot.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	StopAnim->ResolveStateControllerPresentationStateWithPlaybackHold(StopData, StopOneShot);
	TestEqual(TEXT("First release starts one authored Stop"),
		StopOneShot.PresentationState, EProject_JStateControllerPresentationState::TransitionToIdle);
	TestTrue(TEXT("First Stop consumes its movement release"), StopAnim->StateControllerRuntime.bGroundStopConsumed);
	StopData.Combat.bIsCombatMode = true;
	StopData.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	StopAnim->ResolveStateControllerPresentationStateWithPlaybackHold(StopData, StopOneShot);
	TestEqual(TEXT("OTM to Strafe during the same release does not request a second Stop"),
		StopOneShot.PresentationState, EProject_JStateControllerPresentationState::IdleLoop);
	StopData.Input.bHasMoveInput = true;
	StopData.LocomotionContext.bIsMotionMatchingMoving = true;
	StopData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	StopData.Ground.GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
	StopOneShot.bRequested = false;
	StopOneShot.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	StopAnim->ResolveStateControllerPresentationStateWithPlaybackHold(StopData, StopOneShot);
	TestFalse(TEXT("Fresh movement rearms Stop"), StopAnim->StateControllerRuntime.bGroundStopConsumed);
	StopData.Input.bHasMoveInput = false;
	StopData.LocomotionContext.bIsMotionMatchingMoving = false;
	StopData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	StopOneShot.bRequested = true;
	StopOneShot.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	StopAnim->ResolveStateControllerPresentationStateWithPlaybackHold(StopData, StopOneShot);
	TestEqual(TEXT("A later movement release can start a new Stop"),
		StopOneShot.PresentationState, EProject_JStateControllerPresentationState::TransitionToIdle);

	UProject_JCharacterAnimInstance* ActionAnim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	ActionAnim->StateControllerRuntime.PlaybackHoldState = EProject_JStateControllerPresentationState::LocomotionLoop;
	FProject_JAnimThreadSafeData ActionData;
	ActionData.Combat.bIsAttacking = true;
	ActionData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	FProject_JAnimOneShotPresentationThreadSafeData ActionOneShot;
	ActionOneShot.bEnabled = true;
	ActionOneShot.bRequested = true;
	ActionOneShot.PhaseFamily = EProject_JLocomotionPhaseFamily::Stop;
	ActionAnim->ResolveStateControllerPresentationStateWithPlaybackHold(ActionData, ActionOneShot);
	TestTrue(TEXT("A full-body action consumes its hidden Stop"), ActionAnim->StateControllerRuntime.bGroundStopConsumed);
	ActionData.Combat.bIsAttacking = false;
	ActionAnim->ResolveStateControllerPresentationStateWithPlaybackHold(ActionData, ActionOneShot);
	TestEqual(TEXT("An attack exit does not replay its hidden Stop"),
		ActionOneShot.PresentationState, EProject_JStateControllerPresentationState::IdleLoop);
	return true;
}
#endif
