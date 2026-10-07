#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

namespace ProjectJNaturalnessPlayback
{
class FPlayback : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	int32 Scenario = 0, Frame = 0, AirLoopFrames = 0, AirStackFrames = 0, Finalized = 0;
	int32 Searches = 0;
	int32 SteeringFrames = 0;
	int32 PairedCandidateFrames = 0, ContinuedTurnFrames = 0, SelectedGeneralFrames = 0, SelectedCurveFrames = 0;
	int32 GeneralReentries = 0;
	bool bGeneralWasUsed = false, bGeneralCycleReturned = false;
	double MeasuredTickSeconds = 0; int32 MeasuredTicks = 0;
	float MaxVisualYawError = 0.0f;
	FDelegateHandle BoneHandle;
	FString Report;
	static const TCHAR* Label(int32 Case)
	{
		const TCHAR* Names[] = {TEXT("N1_IdleStartStop"), TEXT("N2_SpeedChanges"), TEXT("N3_SmallCurves"),
			TEXT("N4_StrafeFacing"), TEXT("N5_LongFallLand"), TEXT("N6_SourceVisualOutput"), TEXT("C1_CombatRunReversal"), TEXT("G1_GradualOTM"), TEXT("G2_GradualCombatForward"), TEXT("G3_CombatBackward"),
			TEXT("G4_RapidOTM90"), TEXT("G5_RapidCombat90"), TEXT("G6_RapidOTM135"), TEXT("G7_RapidCombat135"),
			TEXT("G8_RapidOTM45"), TEXT("G9_RapidCombat45"), TEXT("G10_CombatCameraOnly"), TEXT("G11_CombatLateral"),
			TEXT("G12_SmallOTM20"), TEXT("G13_SmallCombat20"), TEXT("G14_RepeatOTM30"), TEXT("G15_RepeatCombat30")};
		return Names[Case];
	}
	void Cleanup()
	{
		if (!World) return;
		if (Player) Player->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(BoneHandle);
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		World = nullptr; Player = nullptr;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
		Controller = World->SpawnActor<APlayerController>(Params);
		if (Controller && !FParse::Param(FCommandLine::Get(), TEXT("ProjectJNaturalnessNonLocal"))) Controller->SetAsLocalPlayerController();
		auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
		if (!Controller || !State) return false;
		State->SetOwner(Controller); Controller->SetPlayerState(State);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 1000));
		if (!Player || !Cast<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance())) return false;
		if (Scenario == 3 || Scenario == 6 || Scenario == 8 || Scenario == 9 || Scenario == 11 || Scenario == 13 ||
			(Scenario >= 15 && Scenario <= 17) || Scenario == 19 || Scenario == 21)
		{
			Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
		BoneHandle = Player->GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([this] { ++Finalized; }));
		Report += FString::Printf(TEXT("Scenario=%s Pawn=%s Source=%s AnimClass=%s Local=%d Role=%d\n"),
			Label(Scenario), *Player->GetClass()->GetPathName(), *Player->GetMesh()->GetPathName(),
			*Player->GetMesh()->GetAnimClass()->GetPathName(), Player->IsLocallyControlled(), int32(Player->GetLocalRole()));
		return true;
	}
public:
	explicit FPlayback(FAutomationTestBase* InTest) : Test(InTest)
	{
		Report = TEXT("Controlled NullRHI authored playback; no floor/collision, no rendered visual assessment, no user input latency measurement. CMC is disabled and velocity is prescribed. N6 records transforms only, not grounding quality. SearchDueResultObservations count relevant MM nodes observed with zero elapsed search time; they are not certified search execution counts.\n");
	}
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Authored naturalness fixture setup failed")); Cleanup(); return true; }
		auto* Movement = Player->GetCharacterMovement();
		auto* Locomotion = Player->GetLocomotionAnimStateComponent();
		float Speed = 350, Yaw = 0;
		bool bInput = true;
		if (Scenario == 0)
		{
			bInput = Frame >= 30 && Frame < 120;
			Speed = Frame < 30 || Frame >= 160 ? 0 : Frame < 80 ? (Frame - 30) * 10.0f : Frame < 120 ? 500 : (160 - Frame) * 12.5f;
		}
		if (Scenario == 1)
		{
			Speed = Frame < 45 ? 150 : Frame < 90 ? 500 : Frame < 135 ? 700 : 250;
			if (Frame == 90) Locomotion->HandleSprintStarted();
			if (Frame == 135) Locomotion->HandleSprintStopped();
		}
		if (Scenario == 2) Yaw = 60 * FMath::Sin(Frame / 60.0f);
		if (Scenario == 3) Controller->SetControlRotation(FRotator(0, 60 * FMath::Sin(Frame / 60.0f), 0));
		if (Scenario == 6)
		{
			Yaw = Frame < 60 ? 0 : 180;
			Controller->SetControlRotation(FRotator(0, Yaw, 0));
			Player->SetActorRotation(FRotator(0, Frame < 60 ? 0 : FMath::Min((Frame - 60) * 6.0f, 180.0f), 0));
			bInput = Frame < 140;
			Speed = Frame >= 140 ? 0 : Frame >= 63 && Frame < 70 ? 80 : 350;
		}
		if (Scenario >= 7)
		{
			const float Angle = Scenario == 18 || Scenario == 19 ? 20 : Scenario == 12 || Scenario == 13 ? 135 : Scenario == 14 || Scenario == 15 ? 45 : 90;
			// Rapid correction fixtures include actual path/body lag; angle alone
			// is intentionally insufficient for GeneralTurn admission.
			const float YawPerFrame = Scenario == 11 || Scenario == 13 ? 4.f : Scenario >= 10 ? 3.f : 1.5f;
			float FacingYaw = Frame < 60 ? 0 : FMath::Min((Frame - 60) * YawPerFrame, Angle);
			if (Scenario >= 20 && Frame >= 60)
			{
				const int32 Segment = (Frame - 60) % 60;
				FacingYaw = Segment < 30 ? FMath::Min(Segment * 4.f, 30.f) : FMath::Max(30.f - (Segment - 30) * 4.f, 0.f);
			}
			Yaw = Scenario == 9 ? FacingYaw + 180 : Scenario == 16 ? 0 : Scenario == 17 ? FacingYaw + 90 : FacingYaw;
			Controller->SetControlRotation(FRotator(0, FacingYaw, 0));
			Player->SetActorRotation(FRotator(0, FacingYaw, 0));
			if (Scenario >= 10 && Scenario <= 13 && Frame >= 60)
			{
				const float Lag = FMath::Min(float(Frame - 60) * 10, 55.f) *
					FMath::Clamp(float(115 - Frame) / 15, 0.f, 1.f);
				Player->SetActorRotation(FRotator(0, FacingYaw - Lag, 0));
			}
		}
		if (Scenario == 4)
		{
			if (Frame == 0) Movement->SetMovementMode(MOVE_Falling);
			if (Frame == 240)
			{
				FHitResult Hit; Hit.bBlockingHit = true; Hit.ImpactNormal = FVector::UpVector;
				Locomotion->HandleLanded(Hit);
				Movement->SetMovementMode(MOVE_Walking);
			}
		}
		const FVector Direction = FRotator(0, Yaw, 0).Vector();
		// Camera-only/lateral fixtures must prescribe the same world travel in
		// camera-relative intent and velocity; forward input rotates with camera.
		const float RelativeYaw = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Yaw));
		Locomotion->SetMoveInput(bInput ? (Scenario == 6 || Scenario >= 7 ? FVector2D(FMath::Sin(RelativeYaw), FMath::Cos(RelativeYaw)) : FVector2D(Direction.Y, Direction.X)) : FVector2D::ZeroVector);
		Player->AddMovementInput(Direction, bInput ? 1.0f : 0.0f);
		Movement->Velocity = Direction * Speed;
		if (Scenario >= 10 && Scenario <= 13 && Frame >= 60)
		{
			const float Lag = FMath::Min(float(Frame - 60) * 10, 55.f) * FMath::Clamp(float(115 - Frame) / 15, 0.f, 1.f);
			Movement->Velocity = FRotator(0, Yaw - Lag, 0).Vector() * Speed;
		}
		if (Scenario == 6 && Frame >= 60 && Frame <= 62) Movement->Velocity = FVector(350, 0, 0);
		if (Scenario == 4 && Frame < 240) Movement->Velocity.Z = -700;
		if (Scenario != 3 && Scenario != 6 && Scenario < 7) Player->SetActorRotation(FRotator(0, Yaw, 0));
		Player->AddActorWorldOffset(Movement->Velocity / 60.0f, false);
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		const double TickStarted = FPlatformTime::Seconds();
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (Frame >= 60) { MeasuredTickSeconds += FPlatformTime::Seconds() - TickStarted; ++MeasuredTicks; }
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const float SteeringAlpha = Anim->GetThreadSafeLocomotionSteeringAlpha();
		if (Scenario == 6)
		{
			const auto MM = Anim->GetMotionMatchingDebugSnapshot();
			PairedCandidateFrames += Anim->GetThreadSafeMotionMatchingCandidateCount() == 2;
			ContinuedTurnFrames += MM.SelectionContext.bAllowTurnContinuation;
			if (Anim->GetThreadSafeMotionMatchingCandidateCount() == 2)
				Test->TestTrue(TEXT("Paired frame is an approved local moving turn"), MM.SelectionContext.bMovingTurn180);
			if (Frame == 170) Test->TestEqual(TEXT("Input release closes extra candidates"), Anim->GetThreadSafeMotionMatchingCandidateCount(), 1);
		}
		if (Scenario >= 7)
		{
			const auto MM = Anim->GetMotionMatchingDebugSnapshot();
			PairedCandidateFrames += Anim->GetThreadSafeMotionMatchingCandidateCount() == 2;
			ContinuedTurnFrames += MM.SelectionContext.bAllowGeneralTurnContinuation;
			const FString Database = MM.PostSelection.SelectedDatabase.ToString();
			const bool bGeneral = Database.Contains(TEXT("GeneralTurn"));
			SelectedGeneralFrames += bGeneral;
			if (bGeneral && bGeneralCycleReturned) { ++GeneralReentries; bGeneralCycleReturned = false; }
			bGeneralWasUsed |= bGeneral;
			if (bGeneralWasUsed && !bGeneral && (Database.Contains(TEXT("Cycle")) || Database.Contains(TEXT("Loop")))) bGeneralCycleReturned = true;
			const FString Clip = MM.PostSelection.SelectedAnimation.ToString();
			SelectedCurveFrames += Clip.Contains(TEXT("Arc")) || Clip.Contains(TEXT("Diamond")) || Clip.Contains(TEXT("Hourglass")) || Clip.Contains(TEXT("Box"));
			Test->TestFalse(TEXT("General turn does not require the 180-degree event"), MM.SelectionContext.bMovingTurn180);
			const auto* GeneralCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.GeneralTurnCandidates"));
			if (Scenario == 8 && Frame >= 80 && Frame <= 110 && GeneralCVar && GeneralCVar->GetInt() != 0 && Player->IsLocallyControlled())
				Test->TestFalse(TEXT("Gentle Combat curve searches Dynamic Cycle without Turn candidates"), MM.SelectionContext.bUseSettledCycle);
		}
		if (SteeringAlpha > 0 && !Player->GetMesh()->IsRunningParallelEvaluation())
		{
			++SteeringFrames;
			const auto* Mesh = Player->GetMesh();
			if (Mesh->GetBoneIndex(TEXT("root")) != INDEX_NONE)
				MaxVisualYawError = FMath::Max(MaxVisualYawError, float(FMath::Abs(FMath::FindDeltaAngleDegrees(
					Mesh->GetComponentRotation().Yaw, Mesh->GetSocketTransform(TEXT("root"), RTS_World).Rotator().Yaw))));
		}
		const auto* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass());
		bool bAirStackActive = false;
		for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		{
			if (Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))
			{
				const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
				Searches += Node->GetCachedBlendWeight() > 0 && Node->GetMotionMatchingState().ElapsedPoseSearchTime == 0;
			}
			if (!Property->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct())) continue;
			const auto* Stack = Property->ContainerPtrToValuePtr<FAnimNode_BlendStack>(Anim);
			for (const auto& Entry : Stack->AnimPlayers)
			{
				bAirStackActive |= Entry.GetAnimationAsset() == Anim->GetThreadSafeStateControllerSelectedAnimation() &&
					Entry.IsActive() && Entry.GetBlendInWeight() > 0.9f;
				if (Frame % 15 == 0) Report += FString::Printf(TEXT("Stack Scenario=%s Frame=%d Asset=%s Time=%.4f Weight=%.4f\n"),
					Label(Scenario), Frame, *GetPathNameSafe(Entry.GetAnimationAsset()), Entry.GetCurrentAssetTime(), Entry.GetBlendInWeight());
			}
		}
		if (Scenario == 4 && Frame >= 60 && Frame < 240)
		{
			AirLoopFrames += Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::InAirLoop &&
				Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching();
			AirStackFrames += bAirStackActive;
		}
		if (Frame % 15 == 0)
		{
			const auto MM = Anim->GetMotionMatchingDebugSnapshot();
			Report += FString::Printf(TEXT("Steering Scenario=%s Frame=%d Alpha=%.1f Target=%s OffsetMode=%d MaxVisualYawError=%.3f ContinueTurn=%d Candidates=%d GeneralTurn=%d GeneralContinue=%d\n"),
				Label(Scenario), Frame, SteeringAlpha, *Anim->GetThreadSafeLocomotionSteeringTarget().Rotator().ToString(),
				int32(Anim->GetThreadSafeOffsetRootRotationMode()), MaxVisualYawError, MM.SelectionContext.bAllowTurnContinuation, Anim->GetThreadSafeMotionMatchingCandidateCount(), MM.SelectionContext.bGeneralTurnCandidates, MM.SelectionContext.bAllowGeneralTurnContinuation);
			Report += FString::Printf(TEXT("Pose Scenario=%s Frame=%d Speed=%.1f Phase=%d Presentation=%d Override=%d PSD=%s Clip=%s Time=%.4f SearchDueResultObservations=%d\n"),
				Label(Scenario), Frame, Speed, int32(MM.SelectionContext.PhaseFamily), int32(Anim->GetThreadSafeStateControllerPresentationState()),
				Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching(), *MM.PostSelection.SelectedDatabase.ToString(),
				*MM.PostSelection.SelectedAnimation.ToString(), MM.PostSelection.SelectedAnimationTime, Searches);
			FNodeDebugData Debug(Anim); Anim->GatherDebugData(Debug);
			for (const auto& Row : Debug.GetFlattenedDebugData())
			{
				if (Row.bPoseSource) Report += FString::Printf(TEXT("Branch Scenario=%s Frame=%d Weight=%.4f Node=%s\n"), Label(Scenario), Frame, Row.AbsoluteWeight, *Row.DebugLine);
			}
			for (auto* Mesh : Meshes)
			{
				const bool bPending = Mesh->IsRunningParallelEvaluation();
				const FName LeftFoot = Mesh->GetBoneIndex(TEXT("foot_l")) != INDEX_NONE ? FName(TEXT("foot_l")) : FName(TEXT("Bip01-L-Foot"));
				const FName RightFoot = Mesh->GetBoneIndex(TEXT("foot_r")) != INDEX_NONE ? FName(TEXT("foot_r")) : FName(TEXT("Bip01-R-Foot"));
				Report += FString::Printf(TEXT("Mesh Scenario=%s Frame=%d Name=%s Class=%s Visible=%d Pending=%d Bones=%d ComponentWorld=%s Root=%s FootBoneL=%s FootL=%s FootBoneR=%s FootR=%s\n"),
					Label(Scenario), Frame, *Mesh->GetName(), *GetPathNameSafe(Mesh->GetAnimClass()), Mesh->IsVisible(), Mesh->IsRunningParallelEvaluation(),
					bPending ? 0 : Mesh->GetComponentSpaceTransforms().Num(), *Mesh->GetComponentLocation().ToString(),
					bPending ? TEXT("Pending") : Mesh->GetBoneIndex(TEXT("root")) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(TEXT("root")).ToString(),
					*LeftFoot.ToString(), bPending ? TEXT("Pending") : Mesh->GetBoneIndex(LeftFoot) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(LeftFoot).ToString(),
					*RightFoot.ToString(), bPending ? TEXT("Pending") : Mesh->GetBoneIndex(RightFoot) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(RightFoot).ToString());
				if (Frame == 60)
				{
					TArray<FName> BoneNames; Mesh->GetBoneNames(BoneNames);
					for (FName Name : BoneNames)
					{
						if (Name.ToString().Contains(TEXT("foot")) || Name.ToString().Contains(TEXT("ankle")))
						Report += FString::Printf(TEXT("FootBone Mesh=%s Name=%s\n"), *Mesh->GetName(), *Name.ToString());
					}
				}
			}
		}
		if (++Frame < (Scenario == 4 ? 360 : 180)) return false;
		Test->TestTrue(TEXT("Authored graph finalized source bones"), Finalized > 0);
		if (Scenario == 6)
		{
			Test->TestTrue(TEXT("Actual local producer opens a bounded Cycle/Turn search"), PairedCandidateFrames >= 10);
			Test->TestTrue(TEXT("Actual aligned return grants continuing permission"), ContinuedTurnFrames >= 1);
			Report += FString::Printf(TEXT("ContinuitySummary PairedCandidateFrames=%d ContinuedTurnFrames=%d\n"), PairedCandidateFrames, ContinuedTurnFrames);
		}
		if (Scenario >= 7)
		{
			const auto* GeneralCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.GeneralTurnCandidates"));
			const bool bExpected = GeneralCVar && GeneralCVar->GetInt() != 0 && Scenario >= 10 && Scenario <= 13 && !FParse::Param(FCommandLine::Get(), TEXT("ProjectJNaturalnessNonLocal"));
			if (bExpected)
			{
				Test->TestTrue(TEXT("Authored rapid forward turn opens general candidates"), PairedCandidateFrames >= 5);
				// Cycle may win before the timed quiet finish. That handoff should
				// close the pool immediately without granting an old Turn privilege.
				Test->TestTrue(TEXT("General turn respects visual yaw bound"), MaxVisualYawError <= 45.1f);
				if (Scenario <= 13) Test->TestTrue(TEXT("Rapid 90/135 turn still selects authored GeneralTurn poses"), SelectedGeneralFrames > 0);
				Test->TestFalse(TEXT("Straight tail has released GeneralTurn completion privilege"), Anim->GetMotionMatchingDebugSnapshot().SelectionContext.bAllowGeneralTurnContinuation);
				Test->TestEqual(TEXT("The same authored turn does not re-enter GeneralTurn after its Cycle has won"), GeneralReentries, 0);
			}
			else
			{
				Test->TestEqual(TEXT("Gentle/camera-only/lateral/backward/disabled/nonlocal motion cannot open general candidates"), PairedCandidateFrames, 0);
				Test->TestEqual(TEXT("Those cases cannot select a GeneralTurn database"), SelectedGeneralFrames, 0);
			}
			Report += FString::Printf(TEXT("GeneralTurningSummary Scenario=%s PairedFrames=%d ContinuedFrames=%d SelectedGeneralFrames=%d SelectedCurveFrames=%d GeneralReentries=%d\n"), Label(Scenario), PairedCandidateFrames, ContinuedTurnFrames, SelectedGeneralFrames, SelectedCurveFrames, GeneralReentries);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectLocomotionContinuity")) &&
			!FParse::Param(FCommandLine::Get(), TEXT("ProjectJNaturalnessNonLocal")) && (Scenario == 2 || Scenario == 3))
		{
			Test->TestTrue(TEXT("Actual local movement evaluates Steering"), SteeringFrames >= 30);
			Test->TestTrue(TEXT("Evaluated source root respects the visual yaw bound"), MaxVisualYawError <= 45.1f);
		}
		if (Scenario == 4)
		{
			Test->TestTrue(TEXT("Long fall actually uses external loop output"), AirLoopFrames >= 30);
			Test->TestTrue(TEXT("Actual external stack plays the selected air loop"), AirStackFrames >= 30);
		}
		Report += FString::Printf(TEXT("Summary Scenario=%s Finalizations=%d SearchDueResultObservations=%d AirLoopFrames=%d AirStackFrames=%d\n"),
			Label(Scenario), Finalized, Searches, AirLoopFrames, AirStackFrames);
		Report += FString::Printf(TEXT("SteeringSummary Scenario=%s Frames=%d MaxVisualYawError=%.3f\n"), Label(Scenario), SteeringFrames, MaxVisualYawError);
		Report += FString::Printf(TEXT("TickTiming Scenario=%s WarmupFrames=60 Samples=%d MeanWorldTickMs=%.4f\n"),
			Label(Scenario), MeasuredTicks, MeasuredTicks ? MeasuredTickSeconds * 1000 / MeasuredTicks : 0);
		Cleanup(); Frame = Finalized = Searches = AirLoopFrames = AirStackFrames = 0;
		SteeringFrames = 0; MaxVisualYawError = 0;
		PairedCandidateFrames = ContinuedTurnFrames = SelectedGeneralFrames = SelectedCurveFrames = 0;
		GeneralReentries = 0; bGeneralWasUsed = bGeneralCycleReturned = false;
		MeasuredTickSeconds = 0; MeasuredTicks = 0;
		if (++Scenario < (FParse::Param(FCommandLine::Get(), TEXT("ProjectJGeneralTurningPlayback")) ? 22 : FParse::Param(FCommandLine::Get(), TEXT("ProjectJContinuityPlayback")) ? 7 : 6)) return false;
		FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJNaturalnessPlaybackOutput="), Output);
		if (Output.IsEmpty()) Output = FPaths::ProjectSavedDir() / TEXT("Validation/Naturalness_20261006/Playback.txt");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
		Test->TestTrue(TEXT("Saved controlled authored playback evidence"), FFileHelper::SaveStringToFile(Report, *Output));
		Test->AddInfo(TEXT("Authored playback evidence: ") + Output);
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNaturalnessAuthoredPlayback, "ProjectJ.Animation.Naturalness.AuthoredPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNaturalnessAuthoredPlayback::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJNaturalnessPlayback::FPlayback(this)); return true;
}
#endif
