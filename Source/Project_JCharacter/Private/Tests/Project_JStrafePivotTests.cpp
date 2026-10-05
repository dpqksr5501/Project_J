#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Chooser.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
struct FPivotWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AProject_JGreatswordCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	FPivotWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Player = World->SpawnActor<AProject_JGreatswordCharacter>();
		Controller = World->SpawnActor<APlayerController>();
		Controller->Possess(Player);
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	~FPivotWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafePivotCardinalTest, "ProjectJ.StrafePivot.CardinalSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafePivotCardinalTest::RunTest(const FString&)
{
	FPivotWorld W;
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Combat = NewObject<UProject_JCombatAnimProfile>();
	auto* Locomotion = NewObject<UProject_JLocomotionProfile>();
	Profile->CombatAnimProfile = Combat; Profile->LocomotionProfile = Locomotion;
	W.Player->CharacterAnimProfile = Profile;
	Combat->bEnableCombatStrafeRunPivot = true;
	Locomotion->MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation = true;
	Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Animation_Logic/Chooser/CHT_Player_StateControllerAnimations.CHT_Player_StateControllerAnimations"));
	if (!TestNotNull(TEXT("Authored State Controller root chooser"), Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get())) { return false; }
	const FVector2D Inputs[] = {FVector2D(-1,0), FVector2D(1,0), FVector2D(0,1), FVector2D(0,-1)};
	const EProject_JStateControllerStrafeDirection Directions[] = {
		EProject_JStateControllerStrafeDirection::Left, EProject_JStateControllerStrafeDirection::Right,
		EProject_JStateControllerStrafeDirection::Forward, EProject_JStateControllerStrafeDirection::Backward};
	for (float Yaw : {0.0f, 90.0f, 179.0f})
	{
		W.Controller->SetControlRotation(FRotator(0,Yaw,0)); W.Player->SetActorRotation(FRotator(0,Yaw,0));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
		{
			auto* State = NewObject<UProject_JLocomotionAnimStateComponent>(W.Player);
			State->RefreshCachedReferences(); State->bUseInputDerivedRequestsForRemotePlayers = true;
			const FVector Previous = State->CalculateMoveWorldDirection(Inputs[Index]);
			W.Player->GetCharacterMovement()->Velocity = Previous * 500.0f;
			State->SetMoveInput(Inputs[Index]);
			State->BeginSemanticMoveIntentUpdate();
			State->SetSemanticMoveIntentInput(-Inputs[Index], true);
			FProject_JLocomotionAuthoritativeContext Auth;
			Auth.bCombatMode = true; Auth.RotationMode = EProject_JLocomotionRotationMode::Strafe;
			Auth.GaitIntent = EProject_JLocomotionGaitIntent::Run;
			FProject_JLocomotionKinematicContext Kinematics;
			Kinematics.bHasMoveInput = true; Kinematics.GroundSpeed = 500;
			Kinematics.HorizontalVelocity = Previous * 500;
			Kinematics.MoveWorldDirection = -Previous;
			const FString Case = FString::Printf(TEXT("Yaw %.0f direction %d"), Yaw, Index);
			if (!TestTrue(Case + TEXT(" accepts reversal"), State->IsPivotingForContext(Auth, Kinematics))) { continue; }
			State->AuthoritativeContext = Auth;
			State->bHasMoveInput = true;
			State->GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
			auto& Derived = State->DerivedLocomotionContext;
			Derived.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot;
			Derived.bIsPivoting = true; Derived.bIsMotionMatchingMoving = true;
			Derived.MoveIntentRevision = State->MoveIntentRevision;
			Derived.PivotRequestRevision = State->PivotRequestRevision;
			Derived.PivotPreviousMovementDirection = State->LatchedPivotPreviousMovementDirection;
			Derived.PivotMoveIntentDirection = State->LatchedPivotMoveIntentDirection;
			for (auto Foot : {EProject_JStateControllerFoot::Left, EProject_JStateControllerFoot::Right})
			{
				auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
				Anim->OwningCharacter = W.Player; Anim->OwningPlayerCharacter = W.Player;
				Anim->LocomotionAnimStateComponent = State;
				FProject_JAnimThreadSafeData Data;
				Data.Input.bHasMoveInput = true; Data.Combat.bIsCombatMode = true;
				Data.MotionMatchingSearchPolicy = Locomotion->MotionMatchingSearchPolicy;
				Anim->FillLocomotionStateThreadSafeData(Data);
				TestEqual(Case + TEXT(" previous direction"), Data.OneShotPresentation.PreviousStrafeDirection, Directions[Index]);
				TestEqual(Case + TEXT(" target direction"), Data.OneShotPresentation.StrafeDirection, Directions[Index ^ 1]);
				Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TransitionToLocomotion;
				Data.OneShotPresentation.Foot = Foot;
				Data.Movement.GroundSpeed = 500;
				Data.Ground.GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
				// NativeUpdate publishes these presentation columns before chooser evaluation.
				Anim->StateControllerPresentationStateForChooser = Data.OneShotPresentation.PresentationState;
				Anim->RotationModeForChooser = Auth.RotationMode;
				Anim->GaitIntentForChooser = Auth.GaitIntent;
				Anim->bCombatModeForChooser = true;
				Anim->StateControllerPreviousStrafeDirectionForChooser = Data.OneShotPresentation.PreviousStrafeDirection;
				Anim->StateControllerStrafeDirectionForChooser = Data.OneShotPresentation.StrafeDirection;
				Anim->StateControllerOneShotFootForChooser = Foot;
				Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
				TestTrue(Case + TEXT(" chooses a direct Pivot for either foot"), Data.OneShotPresentation.bHasSelectedAnimation && Anim->StateControllerRuntime.HasCommittedPivot());
				TestTrue(Case + TEXT(" owns external Blend Stack"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
				if (Yaw == 0) { AddInfo(FString::Printf(TEXT("Direction %d Foot %d Asset %s"), Index, int32(Foot), *GetNameSafe(Data.OneShotPresentation.SelectedAnimation.Get()))); }
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafePivotRedirectBasisTest, "ProjectJ.StrafePivot.RedirectBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafePivotRedirectBasisTest::RunTest(const FString&)
{
	FPivotWorld W;
	const FVector Directions[] = {-FVector::RightVector, FVector::RightVector, FVector::ForwardVector, -FVector::ForwardVector};
	for (const FVector& Previous : Directions)
	{
		auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
		Anim->OwningPlayerCharacter = W.Player;
		Anim->StateControllerRuntime.CommitPivot(1, 2, Previous, -Previous);
		Anim->StateControllerOneShotControlYaw = 0; Anim->bHasStateControllerOneShotControlYaw = true;
		Anim->StateControllerOneShotMoveInputYaw = (-Previous).Rotation().Yaw;
		Anim->bHasStateControllerOneShotMoveInputYaw = true;
		W.Controller->SetControlRotation(FRotator::ZeroRotator);
		// A resolved semantic reversal can coexist with the old aggregate IA_Move
		// or the previous CMC input sample. That is not another deliberate redirect.
		W.Player->AddMovementInput(Previous, 1, true); W.Player->ConsumeMovementInputVector();
		TestFalse(TEXT("Held semantic Pivot survives conflicting aggregate input in all four directions"), Anim->ShouldCancelLocalOneShotForInput(true, 25, 30));
		TestTrue(TEXT("Start retains its existing raw movement cancel threshold"), Anim->ShouldCancelLocalOneShotForInput(false, 25, 30));
		W.Controller->SetControlRotation(FRotator(0,30,0));
		TestTrue(TEXT("Camera redirect still cancels Pivot"), Anim->ShouldCancelLocalOneShotForInput(true, 25, 30));
		FProject_JStateControllerRuntime::FPivotIntent Intent;
		Intent.bHasMoveInput = true; Intent.RequestRevision = 1; Intent.MoveIntentRevision = 2;
		TestTrue(TEXT("Held semantic revision preserves Pivot"), Anim->StateControllerRuntime.ReconcilePivot(Intent, 1).bKeepCommittedPivot);
		Intent.MoveIntentRevision = 3;
		TestTrue(TEXT("A later deliberate movement redirect still returns to MM"), Anim->StateControllerRuntime.ReconcilePivot(Intent, 1.1).bForceCycle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafePivotConsecutiveTest, "ProjectJ.StrafePivot.ConsecutiveReversals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafePivotConsecutiveTest::RunTest(const FString&)
{
	FPivotWorld W;
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Combat = NewObject<UProject_JCombatAnimProfile>();
	auto* Locomotion = NewObject<UProject_JLocomotionProfile>();
	Profile->CombatAnimProfile = Combat; Profile->LocomotionProfile = Locomotion;
	W.Player->CharacterAnimProfile = Profile;
	Combat->bEnableCombatStrafeRunPivot = true;
	Locomotion->MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation = true;
	Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Animation_Logic/Chooser/CHT_Player_StateControllerAnimations.CHT_Player_StateControllerAnimations"));
	if (!TestNotNull(TEXT("Actual authored chooser"), Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get())) { return false; }
	const FVector2D Inputs[] = {FVector2D(-1,0), FVector2D(1,0), FVector2D(0,1), FVector2D(0,-1)};
	for (const FVector2D& Initial : Inputs)
	{
		for (auto Foot : {EProject_JStateControllerFoot::Left, EProject_JStateControllerFoot::Right})
		{
			auto* State = NewObject<UProject_JLocomotionAnimStateComponent>(W.Player);
			State->RefreshCachedReferences(); State->bUseInputDerivedRequestsForRemotePlayers = true;
			auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
			Anim->OwningCharacter = W.Player; Anim->OwningPlayerCharacter = W.Player;
			Anim->LocomotionAnimStateComponent = State;
			FVector2D Current = Initial;
			State->SetMoveInput(Current);
			int32 LastSelectionRevision = 0;
			for (int32 Reversal = 0; Reversal < 3; ++Reversal)
			{
				const FVector Previous = State->CalculateMoveWorldDirection(Current);
				W.Player->GetCharacterMovement()->Velocity = Previous * 500;
				State->BeginSemanticMoveIntentUpdate();
				Current = -Current;
				State->SetSemanticMoveIntentInput(Current, true);
				FProject_JLocomotionAuthoritativeContext Auth;
				Auth.bCombatMode = true; Auth.RotationMode = EProject_JLocomotionRotationMode::Strafe;
				Auth.GaitIntent = EProject_JLocomotionGaitIntent::Run;
				FProject_JLocomotionKinematicContext Kinematics;
				Kinematics.bHasMoveInput = true; Kinematics.GroundSpeed = 500;
				Kinematics.HorizontalVelocity = Previous * 500; Kinematics.MoveWorldDirection = -Previous;
				const FString Case = FString::Printf(TEXT("Initial (%g,%g) foot %d reversal %d"), Initial.X, Initial.Y, int32(Foot), Reversal);
				if (!TestTrue(Case + TEXT(" detected"), State->IsPivotingForContext(Auth, Kinematics))) { break; }
				State->AuthoritativeContext = Auth;
				State->bHasMoveInput = true;
				State->GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
				auto& Derived = State->DerivedLocomotionContext;
				Derived.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot;
				Derived.bIsPivoting = true; Derived.bIsMotionMatchingMoving = true;
				Derived.MoveIntentRevision = State->MoveIntentRevision;
				Derived.PivotRequestRevision = State->PivotRequestRevision;
				Derived.PivotPreviousMovementDirection = State->LatchedPivotPreviousMovementDirection;
				Derived.PivotMoveIntentDirection = State->LatchedPivotMoveIntentDirection;
				FProject_JAnimThreadSafeData Data;
				Data.Input.bHasMoveInput = true; Data.Combat.bIsCombatMode = true;
				Data.MotionMatchingSearchPolicy = Locomotion->MotionMatchingSearchPolicy;
				Data.Movement.GroundSpeed = 500; Data.Ground.GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
				Anim->FillLocomotionStateThreadSafeData(Data);
				TestTrue(Case + TEXT(" publishes resolved move input"), Data.Input.bHasMoveInput);
				TestEqual(Case + TEXT(" survives the previous active Pivot"), Data.OneShotPresentation.PhaseFamily, EProject_JLocomotionPhaseFamily::Pivot);
				Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
				Data.OneShotPresentation.Foot = Foot;
				Anim->StateControllerPresentationStateForChooser = Data.OneShotPresentation.PresentationState;
				Anim->RotationModeForChooser = Auth.RotationMode; Anim->GaitIntentForChooser = Auth.GaitIntent;
				Anim->StateControllerStrafeDirectionForChooser = Data.OneShotPresentation.StrafeDirection;
				Anim->StateControllerPreviousStrafeDirectionForChooser = Data.OneShotPresentation.PreviousStrafeDirection;
				Anim->StateControllerOneShotFootForChooser = Foot;
				Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
				TestTrue(Case + TEXT(" selects a direct authored asset"), Data.OneShotPresentation.bHasSelectedAnimation && Data.OneShotPresentation.bShouldOverrideMotionMatching);
				TestEqual(Case + TEXT(" commits the new request"), Anim->StateControllerRuntime.GetPivot().RequestRevision, State->PivotRequestRevision);
				TestTrue(Case + TEXT(" emits one playback command"), Data.OneShotPresentation.bForceBlendNextUpdate);
				TestTrue(Case + TEXT(" advances selection"), Data.OneShotPresentation.SelectionRevision > LastSelectionRevision);
				LastSelectionRevision = Data.OneShotPresentation.SelectionRevision;
				Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
				TestFalse(Case + TEXT(" does not pulse twice"), Data.OneShotPresentation.bForceBlendNextUpdate);
				TestEqual(Case + TEXT(" retains selection on repeat"), Data.OneShotPresentation.SelectionRevision, LastSelectionRevision);
				State->SetMoveInput(Current); // IA_Move catches up before the next reversal.
				Anim->AnimationClock.Advance(0.2f, false); // Still inside the prior Pivot asset.
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotModeContinuityTest, "ProjectJ.Animation.OneShotModeContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotModeContinuityTest::RunTest(const FString&)
{
	FPivotWorld W;
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Locomotion = NewObject<UProject_JLocomotionProfile>();
	Profile->LocomotionProfile = Locomotion; W.Player->CharacterAnimProfile = Profile;
	Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Animation_Logic/Chooser/CHT_Player_StateControllerAnimations.CHT_Player_StateControllerAnimations"));
	if (!TestNotNull(TEXT("Actual authored chooser"), Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get())) { return false; }
	const EProject_JStateControllerPresentationState States[] = {
		EProject_JStateControllerPresentationState::TransitionToLocomotion,
		EProject_JStateControllerPresentationState::TransitionToIdle,
		EProject_JStateControllerPresentationState::TransitionToLand,
		EProject_JStateControllerPresentationState::TransitionToInAir,
		EProject_JStateControllerPresentationState::TurnInPlace};
	for (auto State : States)
	{
		for (bool InitialCombat : {false, true})
		{
			auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
			Anim->OwningCharacter = W.Player; Anim->OwningPlayerCharacter = W.Player;
			auto* HeldAsset = NewObject<UAnimSequence>();
			Anim->CachedStateControllerPresentationState = State;
			Anim->CachedStateControllerChooserTable = Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable;
			Anim->CachedStateControllerSelectedAnimation = HeldAsset;
			Anim->bCachedStateControllerHasSelectedAnimation = true;
			Anim->CachedStateControllerSelectedAnimationOutput.StartTime = 0.7f;
			Anim->CachedStateControllerSelectedAnimationOutput.BlendTime = 0.3f;
			Anim->CachedStateControllerLandingPresentationRevision = 1;
			Anim->CachedStateControllerGaitIntent = EProject_JLocomotionGaitIntent::Run;
			Anim->GaitIntentForChooser = EProject_JLocomotionGaitIntent::Run;
			Anim->StateControllerChooserSelectionRevision = 41;
			Anim->StateControllerRuntime.BeginHold(State, 0);
			FProject_JAnimThreadSafeData Data;
			Data.OneShotPresentation.bEnabled = true; Data.OneShotPresentation.PresentationState = State;
			Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Start;
			Data.Landing.PresentationRevision = 1;
			Data.MotionMatchingSearchPolicy = Locomotion->MotionMatchingSearchPolicy;
			Data.Combat.bIsCombatMode = InitialCombat;
			for (int32 Toggle = 0; Toggle < 4; ++Toggle)
			{
				Data.Combat.bIsCombatMode = !Data.Combat.bIsCombatMode;
				// Combat and rotation can arrive on separate updates, as in user PIE.
				for (int32 Step = 0; Step < 2; ++Step)
				{
					if (Step == 1) { Data.LocomotionContext.RotationMode = Data.Combat.bIsCombatMode ? EProject_JLocomotionRotationMode::Strafe : EProject_JLocomotionRotationMode::OrientToMovement; }
					Data.OneShotPresentation.Foot = EProject_JStateControllerFoot::Right;
					Data.OneShotPresentation.StrafeDirection = EProject_JStateControllerStrafeDirection::Backward;
					Anim->AnimationClock.Advance(0.05f, false);
					Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
					const FString Case = FString::Printf(TEXT("State %d initial combat %d toggle %d step %d"), int32(State), InitialCombat, Toggle, Step);
					TestTrue(Case + TEXT(" retains asset"), Data.OneShotPresentation.SelectedAnimation.Get() == HeldAsset);
					TestEqual(Case + TEXT(" retains StartTime"), Data.OneShotPresentation.SelectedAnimationOutput.StartTime, 0.7f);
					TestEqual(Case + TEXT(" retains command revision"), Data.OneShotPresentation.SelectionRevision, 41);
					TestFalse(Case + TEXT(" emits no replay pulse"), Data.OneShotPresentation.bForceBlendNextUpdate);
					TestEqual(Case + TEXT(" keeps hold clock progressing"), Anim->StateControllerRuntime.GetHoldElapsed(Anim->AnimationClock.Seconds), float(Anim->AnimationClock.Seconds));
				}
			}
			// Real event boundaries must unlock the cache, even with the same state.
			Data.MotionMatchingSearchPolicy.StateControllerAnimationChooserTable = NewObject<UChooserTable>();
			if (State == EProject_JStateControllerPresentationState::TransitionToLand) { ++Data.Landing.PresentationRevision; }
			else if (State == EProject_JStateControllerPresentationState::TurnInPlace) { Anim->StateControllerRuntime.RestartTurnSequence(true, 2, Anim->AnimationClock.Seconds); }
			else if (State == EProject_JStateControllerPresentationState::TransitionToInAir) { Anim->bIsJumpAirReselecting = true; }
			else if (State == EProject_JStateControllerPresentationState::TransitionToIdle) { Data.Input.bHasMoveInput = true; }
			else { Anim->GaitIntentForChooser = EProject_JLocomotionGaitIntent::Sprint; }
			Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
			TestTrue(TEXT("A real replacement event re-evaluates the chooser"), Data.OneShotPresentation.SelectionRevision > 41);
		}
	}
	// Land also exercises the real chooser and hold resolver, not only cache guards.
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
	Anim->OwningCharacter = W.Player; Anim->OwningPlayerCharacter = W.Player;
	FProject_JAnimThreadSafeData Land;
	Land.MotionMatchingSearchPolicy = Locomotion->MotionMatchingSearchPolicy;
	Land.OneShotPresentation.bEnabled = true; Land.OneShotPresentation.bRequested = true;
	Land.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Landing;
	Land.OneShotPresentation.Foot = EProject_JStateControllerFoot::Left;
	Land.Landing.bIsLanding = true; Land.Landing.bUseHeavyLand = true; Land.Landing.PresentationRevision = 1;
	Anim->GaitIntentForChooser = EProject_JLocomotionGaitIntent::Run;
	Anim->StateControllerOneShotFootForChooser = EProject_JStateControllerFoot::Left;
	auto EvaluateLand = [&]()
	{
		Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Land, Land.OneShotPresentation);
		Anim->StateControllerPresentationStateForChooser = Land.OneShotPresentation.PresentationState;
		Anim->RotationModeForChooser = Land.LocomotionContext.RotationMode;
		Anim->EvaluateStateControllerAnimationChooserOnGameThread(Land);
	};
	EvaluateLand();
	if (!TestTrue(TEXT("Actual Land asset selected"), Land.OneShotPresentation.bHasSelectedAnimation)) { return false; }
	UAnimationAsset* Asset = Land.OneShotPresentation.SelectedAnimation.Get();
	const int32 Revision = Land.OneShotPresentation.SelectionRevision;
	const float StartTime = Land.OneShotPresentation.SelectedAnimationOutput.StartTime;
	Land.Combat.bIsCombatMode = true; Anim->AnimationClock.Advance(0.1f, false); EvaluateLand();
	Land.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Anim->AnimationClock.Advance(0.1f, false); EvaluateLand();
	TestTrue(TEXT("Actual Land keeps asset after separate mode edges"), Land.OneShotPresentation.SelectedAnimation.Get() == Asset);
	TestEqual(TEXT("Actual Land keeps selection revision"), Land.OneShotPresentation.SelectionRevision, Revision);
	TestEqual(TEXT("Actual Land keeps StartTime"), Land.OneShotPresentation.SelectedAnimationOutput.StartTime, StartTime);
	TestEqual(TEXT("Actual Land hold advances without restart"), Land.OneShotPresentation.TransitionElapsedTime, 0.2f);
	TestFalse(TEXT("Actual Land does not pulse again"), Land.OneShotPresentation.bForceBlendNextUpdate);
	++Land.Landing.PresentationRevision; EvaluateLand();
	TestTrue(TEXT("A new touchdown emits a new playback command"), Land.OneShotPresentation.bForceBlendNextUpdate);
	TestTrue(TEXT("A new touchdown advances selection"), Land.OneShotPresentation.SelectionRevision > Revision);
	TestEqual(TEXT("A new touchdown begins its own clock"), Land.OneShotPresentation.TransitionElapsedTime, 0.0f);
	Anim->StateControllerRuntime.OnCombatPresentationBoundary(Anim->AnimationClock.Seconds, Land.Landing.PresentationRevision, true);
	EvaluateLand();
	TestTrue(TEXT("Discarded landing cannot re-enter after a full-body boundary"), Land.OneShotPresentation.PresentationState != EProject_JStateControllerPresentationState::TransitionToLand);
	return true;
}
#endif
