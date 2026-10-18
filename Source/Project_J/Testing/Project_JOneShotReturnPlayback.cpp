#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/AnimNode_ProjectJOneShotHandoff.h"
#include "Animation/AnimNode_ProjectJLocomotion.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimNode_ProjectJStrafeUpperBodyFacing.h"
#include "HAL/IConsoleManager.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "HAL/FileManager.h"
#include "RHI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"

namespace ProjectJOneShotReturnPlayback
{
struct FCurveCase { bool bLand, bStrafe, bSprint; FVector2D Keys; int32 Pattern = 0; float Rate = 45.f; };
const TArray<FCurveCase>& CurveCases()
{
	static const TArray<FCurveCase> Cases = {
		{false,false,false,{0,1}}, {false,true,false,{0,1}}, {false,false,true,{0,1}}, {false,true,true,{0,1}},
		{true,false,false,{0,1}}, {true,true,false,{0,1}}, {true,false,true,{0,1}}, {true,true,true,{0,1}},
		{false,true,false,{-1,0}}, {false,true,false,{1,0}}, {false,true,false,{0,-1}},
		{true,true,false,{-1,0}}, {true,true,false,{1,0}}, {true,true,false,{0,-1}},
		{false,true,true,{-1,1}}, {false,true,true,{1,1}}, {true,true,true,{-1,1}}, {true,true,true,{1,1}},
		{false,true,false,{0,1},1}, {true,true,false,{0,1},1}, // brief small input
		{false,true,false,{0,1},2}, {true,true,false,{0,1},2}, // alternating input
		{false,true,false,{0,1},3}, {true,true,false,{0,1},3}, // camera-only, straight world travel
		{false,false,false,{0,1},0,5}, {false,true,false,{0,1},0,5},
		{true,false,false,{0,1},0,5}, {true,true,false,{0,1},0,5},
		{false,false,false,{0,1},0,-10}, {false,true,false,{0,1},0,-10},
		{true,false,false,{0,1},0,-10}, {true,true,false,{0,1},0,-10},
		{false,true,false,{0,1},4}, {true,true,false,{0,1},4}, // sub-degree isolated nudge
		{false,true,false,{0,1},5}, {true,true,false,{0,1},5}, // small repeated pointer noise
		{true,true,false,{-1,1}}, {true,true,false,{1,1}},
		{true,true,false,{-1,-1}}, {true,true,false,{1,-1}}
	};
	return Cases;
}
class FPlayback : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	USceneCaptureComponent2D* Capture = nullptr;
	bool bDirectionCoverage = false, bRender = false;
	FString CaptureDirectory;
	FString EvidenceOutput;
	bool bEvidenceWritable = true;
	int32 PlaybackHz = 60;
	int32 At(int32 FrameAt60Hz) const { return FMath::RoundToInt(FrameAt60Hz * PlaybackHz / 60.f); }
	int32 Scenario = 0, Frame = 0, Returns = 0, WindowFrames = 0, ReturnFrame = -1000;
	int32 SingleScenario = INDEX_NONE;
	int32 LiveFrames = 0, StartReturns = 0, LandReturns = 0;
	int32 InertialFrames = 0;
	bool bWasOverride = false;
	bool bInputResponse = false, bChangedDuringShot = false;
	bool bCurveResponse = false;
	int32 CurveReturns = 0, MicroSettledFrames = 0;
	int32 ShotFrame = -1, CompatibleFrames = 0, ObservedTurnFrames = 0;
	float CameraYaw = 0;
	float CameraPitch = 0;
	float CurveRateOverride = 0;
	float JumpAtSeconds = 1.5f;
	bool bReplayCameraView = false;
	struct FRecordedInput { float Seconds, Yaw, Pitch; bool bInput, bJump; };
	TArray<FRecordedInput> RecordedInput;
	int32 RecordedIndex = 0;
	int32 RecordedJumpCount = 0, CoherentReturnReselections = 0;
	int32 JumpWarpFrames = 0, LandWarpFrames = 0;
	int32 EndFrame() const { return RecordedInput.IsEmpty() ? At(600) : FMath::CeilToInt(RecordedInput.Last().Seconds * PlaybackHz) + 1; }
	bool IsStrafe() const { return bCurveResponse ? CurveCases()[Scenario].bStrafe : Scenario % 2 != 0; }
	bool IsSprint() const { return bDirectionCoverage ? false : bCurveResponse ? CurveCases()[Scenario].bSprint : Scenario % 4 >= 2; }
	int32 Kind() const { return bDirectionCoverage ? 0 : bCurveResponse ? (CurveCases()[Scenario].bLand ? 2 : 0) : bInputResponse ? Scenario / 4 : (Scenario < 4 ? 0 : 2); }
	FName OutgoingClip;
	FVector PreviousFoot[2];
	bool bPreviousFeet = false;
	float PreviousUnifiedTime = -1.f, PreviousUnifiedSearchElapsed = 0.f;
	float PreviousChestYaw = 0.f;
	FQuat PreviousChestRotation = FQuat::Identity;
	bool bPreviousChest = false;
	int32 PreviousUnifiedSelection = INDEX_NONE;
	FString Report;
	void Cleanup()
	{
		if (!World) return;
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		World = nullptr; Player = nullptr; Controller = nullptr; Capture = nullptr;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10000, 10000, 100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0, 0, -100));
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
		Controller = World->SpawnActor<APlayerController>(Params);
		auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
		if (!Controller || !State) return false;
		Controller->SetAsLocalPlayerController(); State->SetOwner(Controller); Controller->SetPlayerState(State);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 110));
		if (!Player) return false;
		// Same-direction continuity: an OTM body initially faces its travel direction.
		// Turning from a fixed forward pose is covered by the input-response fixture.
		if (bDirectionCoverage && !IsStrafe()) Player->SetActorRotation(FRotator(0, (Scenario / 2) * 45.f, 0));
		Player->SetActorEnableCollision(true); Player->GetCharacterMovement()->SetComponentTickEnabled(true);
		if (IsStrafe()) Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		if (bRender && (FParse::Param(FCommandLine::Get(), TEXT("ProjectJOneShotWarpProbe")) ||
			(bCurveResponse ? Scenario == 25 || Scenario == 27 || !RecordedInput.IsEmpty() :
			!bInputResponse && (bDirectionCoverage ? Scenario == 4 || Scenario == 5 : Scenario < 2))))
		{
			if (!Test->TestFalse(TEXT("Render capture requires a real RHI"), GUsingNullRHI)) return false;
			auto* VisualFloor = NewObject<UStaticMeshComponent>(Floor); VisualFloor->SetupAttachment(Box);
			VisualFloor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			VisualFloor->SetRelativeScale3D(FVector(200,200,2)); VisualFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision); VisualFloor->RegisterComponent();
			auto* LightActor = World->SpawnActor<AActor>(); auto* Light = NewObject<UDirectionalLightComponent>(LightActor);
			LightActor->SetRootComponent(Light); Light->SetMobility(EComponentMobility::Movable); Light->SetIntensity(5); Light->RegisterComponent();
			LightActor->SetActorRotation(FRotator(-55,-135,0));
			auto* CaptureActor = World->SpawnActor<AActor>(); Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
			CaptureActor->SetRootComponent(Capture);
			auto* Target = NewObject<UTextureRenderTarget2D>(Capture); Target->RenderTargetFormat = RTF_RGBA8;
			Target->InitCustomFormat(1024,768,PF_B8G8R8A8,false); Target->ClearColor = FLinearColor(.08f,.08f,.08f,1); Target->UpdateResourceImmediate();
			Capture->TextureTarget = Target; Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->FOVAngle = 50; Capture->RegisterComponent();
		}
		Report += FString::Printf(TEXT("Scenario=%d Kind=%s Rotation=%s Gait=%s\n"), Scenario,
			Kind() < 2 ? TEXT("StartStop") : TEXT("Land"), IsStrafe() ? TEXT("Strafe") : TEXT("OTM"), IsSprint() ? TEXT("Sprint") : TEXT("Run"));
		if (bInputResponse && Kind() == 3)
		{
			const auto* Set = Scenario % 2 ? Player->GetCombatStrafeMotionMatchingAssetSet() : Player->GetMotionMatchingAssetSet();
			FProject_JMotionMatchingSelectionContext Capability;
			Capability.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn; Capability.bMovingTurn180 = true;
			Capability.RotationMode = Scenario % 2 ? EProject_JLocomotionRotationMode::Strafe : EProject_JLocomotionRotationMode::OrientToMovement;
			Capability.GaitIntent = Scenario % 4 >= 2 ? EProject_JLocomotionGaitIntent::Sprint : EProject_JLocomotionGaitIntent::Run;
			Capability.bUseGenericFamiliesForNonOrientToMovement = true;
			Test->TestTrue(TEXT("Authored sharp-return pair has compatible schema and normalization"), Set &&
				Set->FindTurnCycleCompanion(Capability, Set->GetDatabaseFamily(Capability.GaitIntent).TurnRedirect));
		}
		return true;
	}
public:
	explicit FPlayback(FAutomationTestBase* InTest, bool bResponse = false, bool bCurve = false, bool bDirections = false, bool bRecorded = false)
		: Test(InTest), bDirectionCoverage(bDirections), bInputResponse(bResponse || bCurve), bCurveResponse(bCurve)
	{
		bRender = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderOneShotReturn"));
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotPlaybackHz="), PlaybackHz);
		bReplayCameraView = FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotCameraPitch="), CameraPitch);
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotCurveRate="), CurveRateOverride);
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotJumpAtSeconds="), JumpAtSeconds);
		FString ReplayPath;
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotCameraReplay="), ReplayPath);
		if (bRecorded)
		{
			ReplayPath = FPaths::ProjectDir() / TEXT("Source/Project_J/Testing/Fixtures/StrafeLandCamera_20261013.csv");
			SingleScenario = Scenario = 5;
		}
		if (!ReplayPath.IsEmpty())
		{
			TArray<FString> Lines;
			if (Test->TestTrue(TEXT("Recorded one-shot input file loads"), FFileHelper::LoadFileToStringArray(Lines, *ReplayPath)))
				for (int32 Index = 1; Index < Lines.Num(); ++Index)
				{
					TArray<FString> Values; Lines[Index].ParseIntoArray(Values, TEXT(","));
					if (!Test->TestEqual(TEXT("Recorded input has five columns"), Values.Num(), 5)) continue;
					RecordedInput.Add({FCString::Atof(*Values[0]), FCString::Atof(*Values[1]), FCString::Atof(*Values[2]),
						FCString::Atof(*Values[3]) > .5f, FCString::Atof(*Values[4]) > .5f});
				}
			Test->TestTrue(TEXT("Recorded input contains movement frames"), RecordedInput.Num() > 1);
			bReplayCameraView = true;
		}
		if (!Test->TestTrue(TEXT("Playback rate is 30, 60 or 120 Hz"), PlaybackHz == 30 || PlaybackHz == 60 || PlaybackHz == 120)) PlaybackHz = 60;
		if (!bRecorded && FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotPlaybackCase="), SingleScenario))
		{
			if (Test->TestTrue(TEXT("Requested playback case exists"), SingleScenario >= 0 &&
				SingleScenario < (bDirections ? 16 : bCurveResponse ? CurveCases().Num() : bInputResponse ? 16 : 8))) Scenario = SingleScenario;
			else SingleScenario = INDEX_NONE;
		}
		else if (!bRecorded && bCurveResponse && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCurveRefinementOnly"))) Scenario = 24;
		Report = FString::Printf(TEXT("Authored Blueprint/GAS/CMC playback on collision floor; NullRHI=%d Hz=%d. FootStepCm is component-space displacement per fixed step, not world foot sliding.\n"), GUsingNullRHI, PlaybackHz);
		FParse::Value(FCommandLine::Get(), bDirections ? TEXT("ProjectJOneShotDirectionOutput=") :
			bCurveResponse ? TEXT("ProjectJOneShotCurveOutput=") : bInputResponse ? TEXT("ProjectJOneShotInputOutput=") : TEXT("ProjectJOneShotReturnOutput="), EvidenceOutput);
		if (EvidenceOutput.IsEmpty())
		{
			EvidenceOutput = FPaths::ProjectSavedDir() / TEXT("Validation/OneShotReturn") /
				FPaths::MakeValidFileName(Test->GetTestFullName()) / FGuid::NewGuid().ToString(EGuidFormats::Digits) / TEXT("Playback.txt");
		}
		// Explicit paths are single-run receipts too. Do not silently replace
		// another fixture's or an earlier run's evidence.
		bEvidenceWritable = Test->TestFalse(TEXT("Evidence output must be new"), IFileManager::Get().FileExists(*EvidenceOutput));
		const FString OutputParent = FPaths::GetPath(EvidenceOutput);
		bEvidenceWritable &= OutputParent.IsEmpty() || Test->TestTrue(TEXT("Evidence parent is available"), IFileManager::Get().MakeDirectory(*OutputParent, true));
		CaptureDirectory = FPaths::GetPath(EvidenceOutput) /
			(bDirections ? TEXT("DirectionFrames") : TEXT("Frames"));
	}
	~FPlayback() { Cleanup(); }
	virtual bool Update() override
	{
		if (!bEvidenceWritable) { Cleanup(); return true; }
		if (!World && !Setup()) { Test->AddError(TEXT("One-shot return fixture setup failed")); Cleanup(); return true; }
		auto* Locomotion = Player->GetLocomotionAnimStateComponent();
		const bool bSprint = IsSprint();
		const bool bLand = Kind() >= 2;
		bool bInput = bLand || (Frame >= At(60) && Frame < At(300));
		FVector2D Keys = bInput ? FVector2D(0, 1) : FVector2D::ZeroVector;
		if (bDirectionCoverage && bInput)
		{
			const float Angle = FMath::DegreesToRadians((Scenario / 2) * 45.f);
			Keys = FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
		}
		if (bCurveResponse && bInput) Keys = CurveCases()[Scenario].Keys.GetSafeNormal();
		if (bInputResponse && ShotFrame >= 0 && Frame > ShotFrame + At(2) && bInput)
		{
			if (bCurveResponse)
			{
				const int32 Pattern = CurveCases()[Scenario].Pattern;
				CameraYaw = Pattern == 1 ? FMath::Min(5.f, CameraYaw + 45.f / PlaybackHz) : Pattern == 2 ? (Frame % 2 ? 2.f : -2.f) :
					Pattern == 4 ? .5f : Pattern == 5 ? .15f * FMath::Sin(Frame * 8.f / PlaybackHz) : CameraYaw +
					(CurveRateOverride != 0 ? CurveRateOverride : CurveCases()[Scenario].Rate) / PlaybackHz;
			}
			else if (Kind() == 0 || Kind() == 2) CameraYaw = FMath::Min(40.f, CameraYaw + 1.5f);
			else if (Kind() == 1) Keys = bSprint ? FVector2D(1, 1).GetSafeNormal() : FVector2D(1, 0);
			else CameraYaw = 180;
			if (bWasOverride && OutgoingClip.ToString().Contains(bLand ? TEXT("Land") : TEXT("Start"))) bChangedDuringShot = true;
		}
		if (!RecordedInput.IsEmpty())
		{
			bool bJumpPressed = Frame == 0 && RecordedInput[0].bJump;
			while (RecordedIndex + 1 < RecordedInput.Num() && RecordedInput[RecordedIndex + 1].Seconds <= Frame / float(PlaybackHz) + .0001f)
			{
				++RecordedIndex;
				bJumpPressed |= RecordedInput[RecordedIndex].bJump;
			}
			const auto& Input = RecordedInput[RecordedIndex];
			CameraYaw = Input.Yaw; CameraPitch = Input.Pitch; bInput = Input.bInput;
			Keys = bInput ? FVector2D(0, 1) : FVector2D::ZeroVector;
			if (bJumpPressed) { Player->Jump(); ++RecordedJumpCount; }
			else Player->StopJumping();
		}
		Controller->SetControlRotation(FRotator(CameraPitch, CameraYaw, 0));
		if (bReplayCameraView && Controller->PlayerCameraManager)
		{
			// A headless controller otherwise returns its pawn's rotation from
			// GetPlayerViewPoint, leaving BaseAimRotation at zero pitch.
			FMinimalViewInfo View = Controller->PlayerCameraManager->GetCameraCacheView();
			View.Location = Player->GetPawnViewLocation(); View.Rotation = Controller->GetControlRotation();
			Controller->PlayerCameraManager->FillCameraCache(View);
		}
		Locomotion->SetMoveInput(Keys);
		Player->UpdateSprintInputFromMove(Keys);
		if (bSprint && Frame == (bLand ? 0 : At(60))) Player->StartSprint();
		if (!bInput) Player->StopSprint();
		const float TravelYaw = bCurveResponse && CurveCases()[Scenario].Pattern == 3 ? 0 : CameraYaw;
		Player->AddMovementInput(FRotator(0, TravelYaw, 0).RotateVector(FVector(Keys.Y, Keys.X, 0)), bInput ? 1.f : 0.f);
		if (RecordedInput.IsEmpty() && bLand && Frame == FMath::RoundToInt(JumpAtSeconds * PlaybackHz))
		{
			// The real input binding publishes semantic Jump before ACharacter::Jump.
			// A bare Jump() instead exercises FallOff in this controlled controller.
			if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJOneShotWarpProbe"))) Locomotion->HandleJumpStarted();
			Player->Jump();
		}
		if (RecordedInput.IsEmpty() && bLand && Frame == FMath::RoundToInt((JumpAtSeconds + .25f) * PlaybackHz)) Player->StopJumping();
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, 1.f / PlaybackHz);
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const bool bOverride = Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching();
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const FName External = GetFNameSafe(Anim->GetThreadSafeStateControllerSelectedAnimation());
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJOneShotWarpProbe")) &&
			bOverride && !Player->GetMesh()->IsRunningParallelEvaluation())
		{
			for (const auto* NodeProperty : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
			{
				if (NodeProperty->Struct->GetFName() != TEXT("AnimNode_OrientationWarping")) continue;
				const void* Node = NodeProperty->ContainerPtrToValuePtr<void>(Anim);
				auto ReadFloat = [&](const TCHAR* Name)
				{
					const auto* Field = FindFProperty<FFloatProperty>(NodeProperty->Struct, Name);
					return Field ? Field->GetPropertyValue_InContainer(Node) : -999.f;
				};
				const auto* AssetField = FindFProperty<FObjectPropertyBase>(NodeProperty->Struct, TEXT("CurrentAnimAsset"));
				const auto* Sequence = AssetField ? Cast<UAnimSequence>(AssetField->GetObjectPropertyValue_InContainer(Node)) : nullptr;
				const float Time = ReadFloat(TEXT("CurrentAnimAssetTime"));
				const FAnimExtractContext Extract(Time, true);
				const float Curve = Sequence ? Sequence->EvaluateCurveData(TEXT("enable_warping"), Extract) : -1.f;
				const FVector RootDelta = Sequence ? Sequence->ExtractRootMotionFromRange(Time, Time + 1.f / PlaybackHz, Extract).GetTranslation() : FVector::ZeroVector;
				// Inactive sample clones retain their previous debug inputs. Count
				// only the currently selected asset, never a stale Start correction.
				if (Sequence == Anim->GetThreadSafeStateControllerSelectedAnimation() &&
					ReadFloat(TEXT("ActualAlpha")) > .01f && FMath::Abs(ReadFloat(TEXT("LocomotionAngle"))) > 20.f)
				{
					JumpWarpFrames += Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToInAir;
					LandWarpFrames += Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToLand;
				}
				Report += FString::Printf(TEXT("WarpProbe Frame=%d State=%d Node=%s Alpha=%.4f ActualAlpha=%.4f Angle=%.3f Asset=%s Time=%.4f Curve=%.4f RootEnabled=%d RootSpeed=%.3f Threshold=%.3f\n"),
					Frame, int32(Anim->GetThreadSafeStateControllerPresentationState()), *NodeProperty->GetName(), ReadFloat(TEXT("Alpha")),
					ReadFloat(TEXT("ActualAlpha")), ReadFloat(TEXT("LocomotionAngle")), *GetNameSafe(Sequence), Time, Curve,
					Sequence && Sequence->HasRootMotion(), RootDelta.Size2D() * PlaybackHz, ReadFloat(TEXT("MinRootMotionSpeedThreshold")));
				if (ReadFloat(TEXT("ActualAlpha")) > .01f && Frame % 5 == 0)
				{
					FNodeDebugData Debug(Anim);
					NodeProperty->ContainerPtrToValuePtr<FAnimNode_SkeletalControlBase>(Anim)->GatherDebugData(Debug);
					for (const auto& Row : Debug.GetFlattenedDebugData())
						if (Row.DebugLine.Contains(TEXT("OrientationWarping")))
							Report += FString::Printf(TEXT("WarpEvaluated Frame=%d %s\n"), Frame, *Row.DebugLine);
				}
			}
		}
		if (bCurveResponse && CurveCases()[Scenario].Pattern >= 4 && Frame >= At(240) && bInput && !bOverride)
		{
			++MicroSettledFrames;
			const auto& Family = Player->GetCombatStrafeMotionMatchingAssetSet()->RunDatabases;
			const auto* Expected = MM.SelectionContext.bUseForwardCycle && Family.ForwardCycle ? Family.ForwardCycle.Get() : Family.SettledCycle.Get();
			Test->TestTrue(TEXT("Micro camera input retains steady movement evidence"), MM.SelectionContext.bUseSettledCycle);
			Test->TestEqual(TEXT("Actual search uses the configured steady forward pool"), MM.PostSelection.SelectedDatabase, GetFNameSafe(Expected));
			Test->TestTrue(TEXT("Micro W input actually selects a steady Loop"), MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Loop_")));
			Test->TestFalse(TEXT("Micro W input cannot select Diamond"), MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("Diamond")));
			if (Frame % At(30) == 0) Report += FString::Printf(TEXT("MicroLoop Scenario=%d Frame=%d PSD=%s Clip=%s\n"), Scenario, Frame,
				*MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString());
		}
		if (bInputResponse && bOverride && ShotFrame < 0 && External.ToString().Contains(bLand ? TEXT("Land") : TEXT("Start"))) ShotFrame = Frame;
		if (bInputResponse && bChangedDuringShot && bOverride && External.ToString().Contains(bLand ? TEXT("Land") : TEXT("Start")) &&
			(Kind() == 0 || Kind() == 2))
		{
			++CompatibleFrames;
			const auto* StateProperty = FindFProperty<FStructProperty>(UProject_JCharacterAnimInstance::StaticClass(), TEXT("ThreadSafeData"));
			if (Test->TestNotNull(TEXT("Authored observation snapshot exists"), StateProperty))
			{
				const auto* Data = StateProperty->ContainerPtrToValuePtr<FProject_JAnimThreadSafeData>(Anim);
				if (Data->GeneralTurnReason == FName(TEXT("OneShotObservation")) && FMath::Abs(Data->GeneralTurnMoveYawRate) > 5) ++ObservedTurnFrames;
				if (bCurveResponse && Frame % 12 == 0)
					Report += FString::Printf(TEXT("CurveProbe Scenario=%d Frame=%d Speed=%.1f Target=%.1f Gain=%.1f Future=%d Remaining=%.2f Phase=%d Position=(%.1f,%.1f)\n"),
						Scenario, Frame, Player->GetVelocity().Size2D(), Player->GetCharacterMovement()->GetMaxSpeed(), Data->Movement.PredictedSpeedGain,
						Data->Movement.bHasFutureTrajectoryVelocity, Data->OneShotPresentation.TransitionTimeRemaining, static_cast<int32>(Data->LocomotionContext.PhaseFamily), Player->GetActorLocation().X, Player->GetActorLocation().Y);
			}
			Test->TestFalse(TEXT("Observation cannot open General Turn underneath the authored shot"), MM.SelectionContext.bGeneralTurnCandidates);
		}
		const bool bReturn = bWasOverride && !bOverride;
		if (bCurveResponse && Anim->DidYieldOneShotForCurveOnGameThread())
		{
			++CurveReturns;
			Test->TestTrue(TEXT("Curve handoff releases an actual owned shot this frame"), bReturn);
			Test->TestEqual(TEXT("Curve only searches its locomotion family, not Turn"), Anim->GetThreadSafeMotionMatchingCandidateCount(), 1);
			Test->TestFalse(TEXT("Curved Strafe does not use the straight settled pool"), MM.SelectionContext.bUseSettledCycle);
			Test->TestFalse(TEXT("Curvature does not open General Turn"), MM.SelectionContext.bGeneralTurnCandidates);
			if (bLand) Test->TestTrue(TEXT("Land minimum hold remains protected"), Locomotion->bCanExitLanding);
			const auto* Set = IsStrafe() ? Player->GetCombatStrafeMotionMatchingAssetSet() : Player->GetMotionMatchingAssetSet();
			const auto& Family = Set->GetDatabaseFamily(bSprint ? EProject_JLocomotionGaitIntent::Sprint : EProject_JLocomotionGaitIntent::Run);
			if (bLand && IsStrafe() && !bSprint && FMath::IsNearlyZero(Keys.X) && Keys.Y > 0 &&
				!MM.SelectionContext.bMovingTurn180 && !MM.SelectionContext.bGeneralTurnCandidates && Family.ForwardCycle)
				Test->TestTrue(TEXT("First aligned Land return uses the same inclusive forward candidates as Cycle"), MM.SelectionContext.bUseForwardCycle);
			bool bActualSearch = false;
			for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
			{
				if (!Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct())) continue;
				const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
				if (Node->GetCachedBlendWeight() <= ZERO_ANIMWEIGHT_THRESH) continue;
				const auto& State = Node->GetMotionMatchingState();
				const bool bRichCycle = State.SearchResult.SelectedDatabase == Family.Cycle ||
					(IsStrafe() && !bSprint && MM.SelectionContext.bUseForwardCycle && Family.ForwardCycle && State.SearchResult.SelectedDatabase == Family.ForwardCycle);
				bActualSearch |= bRichCycle && !State.SearchResult.bIsContinuingPoseSearch && FMath::IsNearlyZero(State.ElapsedPoseSearchTime);
				Report += FString::Printf(TEXT("CurveReturn Scenario=%d Frame=%d ShotFrame=%d SelectedPSD=%s Clip=%s\n"), Scenario, Frame, ShotFrame,
					*GetNameSafe(State.SearchResult.SelectedDatabase.Get()), *GetNameSafe(State.SearchResult.SelectedAnim.Get()));
			}
			Test->TestTrue(TEXT("First curve return actually searches the rich Cycle from Pose History"), bActualSearch);
		}
		if (bReturn)
		{
			++Returns; ReturnFrame = Frame;
			StartReturns += OutgoingClip.ToString().Contains(TEXT("Start"));
			LandReturns += OutgoingClip.ToString().Contains(TEXT("Land"));
			bool bHistoryReady = false;
			for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
			{
				if (!Property->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct())) continue;
				const auto* HistoryNode = Property->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector_Base>(Anim);
				const auto* History = HistoryNode->GetPoseHistoryPtr(); FTransform Root;
				bHistoryReady |= History && History->GetNumEntries() >= 2 && History->GetTransformAtTime(0, Root);
				Report += FString::Printf(TEXT("ReturnHistory Scenario=%d Frame=%d Entries=%d RootValid=%d Candidates=%d SelectedPSD=%s\n"),
					Scenario, Frame, History ? History->GetNumEntries() : 0, bHistoryReady, Anim->GetThreadSafeMotionMatchingCandidateCount(), *MM.PostSelection.SelectedDatabase.ToString());
			}
			Test->TestTrue(TEXT("Return retains real evaluated Pose History rather than resetting it"), bHistoryReady);
			if (bInputResponse && Kind() == 3 && OutgoingClip.ToString().Contains(TEXT("Land")))
			{
				Test->TestEqual(TEXT("Qualified sharp landing return searches Turn and Cycle immediately"), Anim->GetThreadSafeMotionMatchingCandidateCount(), 2);
				bool bActualSearch = false;
				for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
				{
					if (!Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct())) continue;
					const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
					if (Node->GetCachedBlendWeight() <= ZERO_ANIMWEIGHT_THRESH) continue;
					const auto* Databases = FindFProperty<FArrayProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("DatabasesToSearch"));
					if (!Test->TestNotNull(TEXT("Engine database override is reflected"), Databases)) continue;
					FScriptArrayHelper DatabaseView(Databases, Databases->ContainerPtrToValuePtr<void>(Node));
					Test->TestEqual(TEXT("Actual active MM node receives both databases"), DatabaseView.Num(), 2);
					const auto& State = Node->GetMotionMatchingState();
					bActualSearch |= State.SearchResult.SelectedDatabase && !State.SearchResult.bIsContinuingPoseSearch &&
						FMath::IsNearlyZero(State.ElapsedPoseSearchTime);
					Report += FString::Printf(TEXT("FirstReturnSearch Scenario=%d Frame=%d EngineDatabases=%d SelectedPSD=%s Continuing=%d\n"),
						Scenario, Frame, DatabaseView.Num(), *GetNameSafe(State.SearchResult.SelectedDatabase.Get()), State.SearchResult.bIsContinuingPoseSearch);
				}
				Test->TestTrue(TEXT("The very first returned frame actually executes a fresh engine search"), bActualSearch);
			}
		}
		if (bOverride) OutgoingClip = External;
		const bool bWindow = Frame - ReturnFrame <= At(18);
		const bool bWarpCapture = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOneShotWarpProbe")) && bOverride &&
			(Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToInAir ||
			 Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToLand) && Frame % 6 == 0;
		if (Capture && (bWarpCapture || Frame == At(120) || (bWindow && (Frame - ReturnFrame) % FMath::Max(1, At(3)) == 0)))
		{
			const FVector Center = Player->GetActorLocation() + FVector(0,0,-10);
			const FVector Position = Center + FVector(350,350,130);
			Capture->SetWorldLocationAndRotation(Position, (Center-Position).Rotation()); Capture->CaptureScene();
			IFileManager::Get().MakeDirectory(*CaptureDirectory, true);
			UKismetRenderingLibrary::ExportRenderTarget(World, Capture->TextureTarget, CaptureDirectory,
				FString::Printf(TEXT("Scenario%d_Frame%d.png"), Scenario, Frame));
		}
		if (bWindow) ++WindowFrames;
		if (bWindow && !Player->GetMesh()->IsRunningParallelEvaluation())
		{
			FNodeDebugData Debug(Anim);
			for (const auto* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
				if (Property->Struct->IsChildOf(FAnimNode_Inertialization::StaticStruct()))
					Property->ContainerPtrToValuePtr<FAnimNode_Inertialization>(Anim)->GatherDebugData(Debug);
			for (const auto& Row : Debug.GetFlattenedDebugData())
			{
				if (!Row.DebugLine.Contains(TEXT("AnimNode_Inertialization")) ||
					!Row.DebugLine.Contains(TEXT("EInertializationState::Active"))) continue;
				++InertialFrames;
				Report += FString::Printf(TEXT("Inertial Scenario=%d Frame=%d Weight=%.3f Node=%s\n"), Scenario, Frame, Row.AbsoluteWeight, *Row.DebugLine);
			}
		}
		for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
		{
			if (Property->Struct->IsChildOf(FAnimNode_ProjectJLocomotion::StaticStruct()))
			{
				const auto* Unified = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJLocomotion>(Anim);
				Test->TestEqual(TEXT("Shared player follows authored ownership"), Unified->IsPlayingOneShot(), bOverride);
				if (bOverride) Test->TestEqual(TEXT("A new authored owner cancels the prior inertial return envelope"), Unified->GetInertialReturnSecondsRemaining(), 0.f);
				if (Unified->GetInertialReturnSecondsRemaining() > UE_SMALL_NUMBER)
				{
					Test->TestEqual(TEXT("Inertial return cannot start a live stack blend beneath its pose offset"), Unified->AnimPlayers.Num(), 1);
					if (!bReturn && Unified->AnyNewBlendToThisFrame()) ++CoherentReturnReselections;
				}
				if (bOverride && !Unified->AnimPlayers.IsEmpty())
				{
					Test->TestEqual(TEXT("Authored command is the actual shared player"), GetFNameSafe(Unified->GetAnimAsset()), External);
					if (PreviousUnifiedSelection == Unified->GetOneShotSelectionRevision() && PreviousUnifiedTime >= 0.f &&
						!Unified->AnimPlayers[0].IsLooping())
					{
						Test->TestTrue(TEXT("Unchanged authored command advances, never restarts"), Unified->GetCurrentAssetTime() + UE_SMALL_NUMBER >= PreviousUnifiedTime);
						Test->TestEqual(TEXT("Authored playback does not update MM search clock"), Unified->GetMotionMatchingState().ElapsedPoseSearchTime, PreviousUnifiedSearchElapsed);
					}
					PreviousUnifiedTime = Unified->GetCurrentAssetTime();
					PreviousUnifiedSearchElapsed = Unified->GetMotionMatchingState().ElapsedPoseSearchTime;
					PreviousUnifiedSelection = Unified->GetOneShotSelectionRevision();
				}
				else { PreviousUnifiedTime = -1.f; PreviousUnifiedSelection = INDEX_NONE; }
				if (Unified->IsReturning())
				{
					++LiveFrames;
					bool bActualOutgoing = false;
					for (int32 Index = 1; Index < Unified->AnimPlayers.Num(); ++Index)
					{
						const auto& Sample = Unified->AnimPlayers[Index];
						if (GetFNameSafe(Sample.GetAnimationAsset()) != OutgoingClip) continue;
						bActualOutgoing = true;
						if (bReturn && OutgoingClip.ToString().Contains(TEXT("Start")))
							Test->TestTrue(TEXT("Start returns before the outgoing player freezes"), Sample.GetCurrentAssetTime() < Sample.GetCurrentAssetLength());
					}
					Test->TestTrue(TEXT("One stack retains the real outgoing authored player"), bActualOutgoing);
				}
				if (bReturn || bWindow || (bOverride && Frame % 10 == 0))
				{
					Report += FString::Printf(TEXT("Unified Scenario=%d Frame=%d Shot=%d Returning=%d OutgoingWeight=%.4f Players=%d ActualTime=%.4f Remaining=%.4f InertialReturnRemaining=%.4f\n"),
						Scenario, Frame, Unified->IsPlayingOneShot(), Unified->IsReturning(), Unified->GetOutgoingWeight(),
						Unified->AnimPlayers.Num(), Unified->GetCurrentAssetTime(), Unified->GetOneShotSecondsRemaining(), Unified->GetInertialReturnSecondsRemaining());
					const auto* PresentationProperty = FindFProperty<FStructProperty>(FAnimNode_ProjectJLocomotion::StaticStruct(), TEXT("Presentation"));
					if (PresentationProperty)
					{
						const auto* Presentation = PresentationProperty->ContainerPtrToValuePtr<FProject_JAnimOneShotPresentationThreadSafeData>(Unified);
						Report += FString::Printf(TEXT("  Authored Clip=%s Warp=%d Direction=%.4f\n"),
							*GetNameSafe(Presentation->SelectedAnimation), Presentation->bShouldEnableCombatStrafeOrientationWarping, Presentation->StrafeDirectionAngle);
					}
				}
				if (Frame == EndFrame() - 1)
				{
					Test->TestFalse(TEXT("Completed shared returns release presentation"), Unified->IsReturning());
					Test->TestEqual(TEXT("Completed return envelope has no stale owner"), Unified->GetInertialReturnSecondsRemaining(), 0.f);
				}
				continue;
			}
			if (!Property->Struct->IsChildOf(FAnimNode_ProjectJOneShotHandoff::StaticStruct())) continue;
			const auto* Handoff = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJOneShotHandoff>(Anim);
			const float Weight = Handoff->GetOutgoingWeight();
			if (Handoff->IsLiveReturnActive() && Weight > ZERO_ANIMWEIGHT_THRESH)
			{
				++LiveFrames;
				Report += FString::Printf(TEXT("LiveReturn Scenario=%d Frame=%d ExternalWeight=%.4f\n"), Scenario, Frame, Weight);
				bool bFoundOutgoing = false;
				for (const FStructProperty* StackProperty : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
				{
					if (!StackProperty->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct())) continue;
					const auto* Stack = StackProperty->ContainerPtrToValuePtr<FAnimNode_BlendStack>(Anim);
					bFoundOutgoing |= !Stack->AnimPlayers.IsEmpty() && GetFNameSafe(Stack->AnimPlayers[0].GetAnimationAsset()) == OutgoingClip;
					if (!Stack->AnimPlayers.IsEmpty() && GetFNameSafe(Stack->AnimPlayers[0].GetAnimationAsset()) == OutgoingClip)
						Report += FString::Printf(TEXT("ExternalActual Scenario=%d Frame=%d Time=%.4f Length=%.4f Rate=%.3f\n"),
							Scenario, Frame, Stack->GetCurrentAssetTime(), Stack->GetCurrentAssetLength(), Stack->AnimPlayers[0].GetPlayRate());
				}
				Test->TestTrue(TEXT("Live overlap retains the actual outgoing external asset"), bFoundOutgoing);
			}
			if (Frame == EndFrame() - 1) Test->TestFalse(TEXT("Completed returns release their retained command"), Handoff->IsLiveReturnActive());
		}
		FVector Feet[2]; bool bFeet = !Player->GetMesh()->IsRunningParallelEvaluation();
		const int32 ChestBone = Player->GetMesh()->GetBoneIndex(TEXT("spine_05"));
		const bool bChest = !Player->GetMesh()->IsRunningParallelEvaluation() && Player->GetMesh()->GetComponentSpaceTransforms().IsValidIndex(ChestBone);
		const float ChestYaw = bChest ? Player->GetMesh()->GetComponentSpaceTransforms()[ChestBone].GetRotation().Rotator().Yaw : 0.f;
		const FQuat ChestRotation = bChest ? Player->GetMesh()->GetComponentSpaceTransforms()[ChestBone].GetRotation() : FQuat::Identity;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const int32 Bone = Player->GetMesh()->GetBoneIndex(Index ? TEXT("foot_r") : TEXT("foot_l"));
			bFeet &= Player->GetMesh()->GetComponentSpaceTransforms().IsValidIndex(Bone);
			if (bFeet) Feet[Index] = Player->GetMesh()->GetComponentSpaceTransforms()[Bone].GetLocation();
		}
		if (bReturn || bWindow || (bOverride && Frame % 10 == 0))
		{
			for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
				if (Property->Struct == FAnimNode_ProjectJStrafeUpperBodyFacing::StaticStruct())
				{
					const auto* Facing = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJStrafeUpperBodyFacing>(Anim);
					Report += FString::Printf(TEXT("Torso Scenario=%d Frame=%d Enabled=%d Yaw=%.4f Error=%.4f Desired=%.4f Applied=%.4f UprightWeight=%.4f\n"),
						Scenario, Frame, Facing->WasEnabled(), Facing->GetInputChestYaw(), Facing->GetFacingError(), Facing->GetDesiredYaw(), Facing->GetAppliedYaw(), Facing->GetUprightWeight());
				}
			Report += FString::Printf(TEXT("Chest Scenario=%d Frame=%d Valid=%d Yaw=%.4f Step=%.4f Angle3D=%.4f\n"), Scenario, Frame, bChest, ChestYaw,
				bChest && bPreviousChest ? FMath::FindDeltaAngleDegrees(PreviousChestYaw, ChestYaw) : 0.f,
				bChest && bPreviousChest ? FMath::RadiansToDegrees(PreviousChestRotation.AngularDistance(ChestRotation)) : 0.f);
			Report += FString::Printf(TEXT("Frame Scenario=%d Frame=%d Return=%d Since=%d Override=%d Outgoing=%s PSD=%s Clip=%s Time=%.4f FootStepL=%.3f FootStepR=%.3f\n"),
				Scenario, Frame, bReturn, Frame - ReturnFrame, bOverride, *OutgoingClip.ToString(),
				*MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString(), MM.PostSelection.SelectedAnimationTime,
				bFeet && bPreviousFeet ? FVector::Dist(Feet[0], PreviousFoot[0]) : -1.f,
				bFeet && bPreviousFeet ? FVector::Dist(Feet[1], PreviousFoot[1]) : -1.f);
			for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
			{
				if (!Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct())) continue;
				const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
				Report += FString::Printf(TEXT("  MM Weight=%.3f SearchElapsed=%.4f Players=%d\n"), Node->GetCachedBlendWeight(), Node->GetMotionMatchingState().ElapsedPoseSearchTime, Node->AnimPlayers.Num());
				for (const auto& Entry : Node->AnimPlayers)
					Report += FString::Printf(TEXT("  Player Clip=%s Time=%.4f Weight=%.4f BlendElapsed=%.4f BlendTime=%.4f\n"),
						*GetNameSafe(Entry.GetAnimationAsset()), Entry.GetCurrentAssetTime(), Entry.GetBlendInWeight(), Entry.GetCurrentBlendInTime(), Entry.GetTotalBlendInTime());
			}
		}
		bPreviousFeet = bFeet;
		bPreviousChest = bChest; PreviousChestYaw = ChestYaw;
		PreviousChestRotation = ChestRotation;
		if (bFeet) { PreviousFoot[0] = Feet[0]; PreviousFoot[1] = Feet[1]; }
		bWasOverride = bOverride;
		if (++Frame < EndFrame()) return false;
		Test->TestTrue(FString::Printf(TEXT("Scenario %d actually returns from an authored external clip"), Scenario), Returns > 0);
		Test->TestTrue(TEXT("Return window evaluates actual bones"), WindowFrames >= 12);
		if (!bLand) Test->TestTrue(TEXT("Actually returns from Start, not merely Stop"), StartReturns > 0);
		else Test->TestTrue(TEXT("Actually returns from Land, not merely airborne loop"), LandReturns > 0);
		const auto* LiveCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.LiveOneShotReturn"));
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectOneShotHandoff")) && LiveCVar && LiveCVar->GetInt() != 0)
			Test->TestTrue(TEXT("Prepared graph actually retains outgoing samples or runs engine inertialization"), LiveFrames + InertialFrames >= 3);
		Report += FString::Printf(TEXT("Summary Scenario=%d Returns=%d WindowFrames=%d StartReturns=%d LandReturns=%d LiveFrames=%d InertialFrames=%d\n"), Scenario, Returns, WindowFrames, StartReturns, LandReturns, LiveFrames, InertialFrames);
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJOneShotWarpProbe")) && bCurveResponse &&
			IsStrafe() && bLand && FMath::Abs(CurveCases()[Scenario].Keys.X) > .1f)
		{
			Test->TestTrue(TEXT("Diagonal jump traverses an active orientation correction"), JumpWarpFrames > 0);
			Test->TestTrue(TEXT("Diagonal landing traverses an active orientation correction"), LandWarpFrames > 0);
			Report += FString::Printf(TEXT("WarpSummary JumpFrames=%d LandFrames=%d\n"), JumpWarpFrames, LandWarpFrames);
		}
		if (bInputResponse)
		{
			Test->TestTrue(TEXT("Input was changed during an actual authored Start/Land"), bChangedDuringShot);
			if (!bCurveResponse && (Kind() == 0 || Kind() == 2))
			{
				Test->TestTrue(TEXT("Compatible small camera input preserves multiple live authored frames"), CompatibleFrames >= 2);
				Test->TestTrue(TEXT("Actual authored shot observes turn geometry before returning"), ObservedTurnFrames >= 2);
			}
			Report += FString::Printf(TEXT("InputResponse Scenario=%d Kind=%d ShotFrame=%d CompatibleFrames=%d ObservedTurnFrames=%d\n"), Scenario, Kind(), ShotFrame, CompatibleFrames, ObservedTurnFrames);
		}
		if (bCurveResponse)
		{
			if (CurveCases()[Scenario].Pattern >= 4) Test->TestTrue(TEXT("Micro input is checked in actual settled MM playback"), MicroSettledFrames >= At(30));
			if (RecordedInput.IsEmpty())
				Test->TestEqual(FString::Printf(TEXT("Scenario %d yields once for sustained travel, never for transient/noise/camera-only"), Scenario), CurveReturns, CurveCases()[Scenario].Pattern == 0 ? 1 : 0);
			else Test->TestTrue(TEXT("Recorded slow curves actually release authored playback"), CurveReturns > 0);
			Report += FString::Printf(TEXT("CurveSummary Scenario=%d Pattern=%d Keys=(%.1f,%.1f) CurveReturns=%d\n"), Scenario, CurveCases()[Scenario].Pattern,
				CurveCases()[Scenario].Keys.X, CurveCases()[Scenario].Keys.Y, CurveReturns);
		}
		if (!RecordedInput.IsEmpty())
		{
			Test->TestTrue(TEXT("Recorded jumps produce actual Land returns"), LandReturns >= RecordedJumpCount);
			Test->TestTrue(TEXT("Recorded return actually reselects during its coherent blend"), CoherentReturnReselections > 0);
			Report += FString::Printf(TEXT("RecordedSummary Jumps=%d LandReturns=%d CoherentReselections=%d\n"), RecordedJumpCount, LandReturns, CoherentReturnReselections);
		}
		Cleanup(); Frame = Returns = WindowFrames = 0; ReturnFrame = -1000; bWasOverride = bPreviousFeet = false; OutgoingClip = NAME_None;
		PreviousUnifiedTime = -1.f; PreviousUnifiedSelection = INDEX_NONE;
		bPreviousChest = false;
		LiveFrames = StartReturns = LandReturns = 0;
		InertialFrames = 0;
		ShotFrame = -1; CompatibleFrames = ObservedTurnFrames = 0; CameraYaw = 0; bChangedDuringShot = false;
		CurveReturns = MicroSettledFrames = 0;
		JumpWarpFrames = LandWarpFrames = 0;
		if (++Scenario < (bDirectionCoverage ? 16 : bCurveResponse ? CurveCases().Num() : bInputResponse ? 16 : 8) && SingleScenario == INDEX_NONE) return false;
		Test->TestTrue(TEXT("Saved frame-by-frame return evidence"), FFileHelper::SaveStringToFile(Report, *EvidenceOutput));
		Test->AddInfo(TEXT("One-shot return evidence: ") + EvidenceOutput); return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotReturnPlayback, "ProjectJ.Animation.OneShotReturn.AuthoredPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotReturnPlayback::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJOneShotReturnPlayback::FPlayback(this)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotDirectionPlayback, "ProjectJ.Animation.OneShotReturn.DirectionPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotDirectionPlayback::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJOneShotReturnPlayback::FPlayback(this, false, false, true)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotInputPlayback, "ProjectJ.Animation.OneShotInput.AuthoredPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotInputPlayback::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJOneShotReturnPlayback::FPlayback(this, true)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotCurvePlayback, "ProjectJ.Animation.OneShotInput.AuthoredCurvePlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotCurvePlayback::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJOneShotReturnPlayback::FPlayback(this, true, true)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRecordedLandReturn, "ProjectJ.Animation.OneShotReturn.RecordedStrafeLand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRecordedLandReturn::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ProjectJOneShotReturnPlayback::FPlayback(this, true, true, false, true)); return true;
}
#endif
