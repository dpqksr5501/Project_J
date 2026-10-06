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
	int32 PairedCandidateFrames = 0, ContinuedTurnFrames = 0;
	float MaxVisualYawError = 0.0f;
	FDelegateHandle BoneHandle;
	FString Report;
	static const TCHAR* Label(int32 Case)
	{
		const TCHAR* Names[] = {TEXT("N1_IdleStartStop"), TEXT("N2_SpeedChanges"), TEXT("N3_SmallCurves"),
			TEXT("N4_StrafeFacing"), TEXT("N5_LongFallLand"), TEXT("N6_SourceVisualOutput"), TEXT("C1_CombatRunReversal")};
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
		if (Scenario == 3 || Scenario == 6)
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
		Locomotion->SetMoveInput(bInput ? (Scenario == 6 ? FVector2D(0, 1) : FVector2D(Direction.Y, Direction.X)) : FVector2D::ZeroVector);
		Player->AddMovementInput(Direction, bInput ? 1.0f : 0.0f);
		Movement->Velocity = Direction * Speed;
		if (Scenario == 6 && Frame >= 60 && Frame <= 62) Movement->Velocity = FVector(350, 0, 0);
		if (Scenario == 4 && Frame < 240) Movement->Velocity.Z = -700;
		if (Scenario != 3 && Scenario != 6) Player->SetActorRotation(FRotator(0, Yaw, 0));
		Player->AddActorWorldOffset(Movement->Velocity / 60.0f, false);
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
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
			Report += FString::Printf(TEXT("Steering Scenario=%s Frame=%d Alpha=%.1f Target=%s OffsetMode=%d MaxVisualYawError=%.3f ContinueTurn=%d Candidates=%d\n"),
				Label(Scenario), Frame, SteeringAlpha, *Anim->GetThreadSafeLocomotionSteeringTarget().Rotator().ToString(),
				int32(Anim->GetThreadSafeOffsetRootRotationMode()), MaxVisualYawError, MM.SelectionContext.bAllowTurnContinuation, Anim->GetThreadSafeMotionMatchingCandidateCount());
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
		Cleanup(); Frame = Finalized = Searches = AirLoopFrames = AirStackFrames = 0;
		SteeringFrames = 0; MaxVisualYawError = 0;
		PairedCandidateFrames = ContinuedTurnFrames = 0;
		if (++Scenario < (FParse::Param(FCommandLine::Get(), TEXT("ProjectJContinuityPlayback")) ? 7 : 6)) return false;
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
