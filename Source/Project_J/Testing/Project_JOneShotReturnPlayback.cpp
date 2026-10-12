#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_ProjectJOneShotHandoff.h"
#include "Animation/AnimNode_ProjectJLocomotion.h"
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
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"

namespace ProjectJOneShotReturnPlayback
{
struct FCurveCase { bool bLand, bStrafe, bSprint; FVector2D Keys; int32 Pattern = 0; };
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
		{false,true,false,{0,1},3}, {true,true,false,{0,1},3}  // camera-only, straight world travel
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
	int32 PlaybackHz = 60;
	int32 At(int32 FrameAt60Hz) const { return FMath::RoundToInt(FrameAt60Hz * PlaybackHz / 60.f); }
	int32 Scenario = 0, Frame = 0, Returns = 0, WindowFrames = 0, ReturnFrame = -1000;
	int32 LiveFrames = 0, StartReturns = 0, LandReturns = 0;
	bool bWasOverride = false;
	bool bInputResponse = false, bChangedDuringShot = false;
	bool bCurveResponse = false;
	int32 CurveReturns = 0;
	int32 ShotFrame = -1, CompatibleFrames = 0, ObservedTurnFrames = 0;
	float CameraYaw = 0;
	bool IsStrafe() const { return bCurveResponse ? CurveCases()[Scenario].bStrafe : Scenario % 2 != 0; }
	bool IsSprint() const { return bDirectionCoverage ? false : bCurveResponse ? CurveCases()[Scenario].bSprint : Scenario % 4 >= 2; }
	int32 Kind() const { return bDirectionCoverage ? 0 : bCurveResponse ? (CurveCases()[Scenario].bLand ? 2 : 0) : bInputResponse ? Scenario / 4 : (Scenario < 4 ? 0 : 2); }
	FName OutgoingClip;
	FVector PreviousFoot[2];
	bool bPreviousFeet = false;
	float PreviousUnifiedTime = -1.f, PreviousUnifiedSearchElapsed = 0.f;
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
		if (bRender && !bInputResponse && (bDirectionCoverage ? Scenario == 4 || Scenario == 5 : Scenario < 2))
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
	explicit FPlayback(FAutomationTestBase* InTest, bool bResponse = false, bool bCurve = false, bool bDirections = false)
		: Test(InTest), bDirectionCoverage(bDirections), bInputResponse(bResponse || bCurve), bCurveResponse(bCurve)
	{
		bRender = FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderOneShotReturn"));
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJOneShotPlaybackHz="), PlaybackHz);
		if (!Test->TestTrue(TEXT("Playback rate is 30, 60 or 120 Hz"), PlaybackHz == 30 || PlaybackHz == 60 || PlaybackHz == 120)) PlaybackHz = 60;
		Report = FString::Printf(TEXT("Authored Blueprint/GAS/CMC playback on collision floor; NullRHI=%d Hz=%d. FootStepCm is component-space displacement per fixed step, not world foot sliding.\n"), GUsingNullRHI, PlaybackHz);
		FString Output;
		FParse::Value(FCommandLine::Get(), bDirections ? TEXT("ProjectJOneShotDirectionOutput=") : TEXT("ProjectJOneShotReturnOutput="), Output);
		CaptureDirectory = (Output.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Validation/OneShotReturn") : FPaths::GetPath(Output)) /
			(bDirections ? TEXT("DirectionFrames") : TEXT("Frames"));
	}
	~FPlayback() { Cleanup(); }
	virtual bool Update() override
	{
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
				CameraYaw = Pattern == 1 ? FMath::Min(5.f, CameraYaw + .75f) : Pattern == 2 ? (Frame % 2 ? 2.f : -2.f) : CameraYaw + .75f;
			}
			else if (Kind() == 0 || Kind() == 2) CameraYaw = FMath::Min(40.f, CameraYaw + 1.5f);
			else if (Kind() == 1) Keys = bSprint ? FVector2D(1, 1).GetSafeNormal() : FVector2D(1, 0);
			else CameraYaw = 180;
			if (bWasOverride && OutgoingClip.ToString().Contains(bLand ? TEXT("Land") : TEXT("Start"))) bChangedDuringShot = true;
		}
		Controller->SetControlRotation(FRotator(0, CameraYaw, 0));
		Locomotion->SetMoveInput(Keys);
		Player->UpdateSprintInputFromMove(Keys);
		if (bSprint && Frame == (bLand ? 0 : At(60))) Player->StartSprint();
		if (!bInput) Player->StopSprint();
		const float TravelYaw = bCurveResponse && CurveCases()[Scenario].Pattern == 3 ? 0 : CameraYaw;
		Player->AddMovementInput(FRotator(0, TravelYaw, 0).RotateVector(FVector(Keys.Y, Keys.X, 0)), bInput ? 1.f : 0.f);
		if (bLand && Frame == At(90)) Player->Jump();
		if (bLand && Frame == At(105)) Player->StopJumping();
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, 1.f / PlaybackHz);
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const bool bOverride = Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching();
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const FName External = GetFNameSafe(Anim->GetThreadSafeStateControllerSelectedAnimation());
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
			const UPoseSearchDatabase* Expected = Set->GetDatabaseFamily(bSprint ? EProject_JLocomotionGaitIntent::Sprint : EProject_JLocomotionGaitIntent::Run).Cycle;
			bool bActualSearch = false;
			for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
			{
				if (!Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct())) continue;
				const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(Anim);
				if (Node->GetCachedBlendWeight() <= ZERO_ANIMWEIGHT_THRESH) continue;
				const auto& State = Node->GetMotionMatchingState();
				bActualSearch |= State.SearchResult.SelectedDatabase == Expected && !State.SearchResult.bIsContinuingPoseSearch && FMath::IsNearlyZero(State.ElapsedPoseSearchTime);
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
		if (Capture && (Frame == At(120) || (bWindow && (Frame - ReturnFrame) % FMath::Max(1, At(3)) == 0)))
		{
			const FVector Center = Player->GetActorLocation() + FVector(0,0,-10);
			const FVector Position = Center + FVector(350,350,130);
			Capture->SetWorldLocationAndRotation(Position, (Center-Position).Rotation()); Capture->CaptureScene();
			IFileManager::Get().MakeDirectory(*CaptureDirectory, true);
			UKismetRenderingLibrary::ExportRenderTarget(World, Capture->TextureTarget, CaptureDirectory,
				FString::Printf(TEXT("Scenario%d_Frame%d.png"), Scenario, Frame));
		}
		if (bWindow) ++WindowFrames;
		for (const FStructProperty* Property : IAnimClassInterface::GetFromClass(Anim->GetClass())->GetAnimNodeProperties())
		{
			if (Property->Struct->IsChildOf(FAnimNode_ProjectJLocomotion::StaticStruct()))
			{
				const auto* Unified = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJLocomotion>(Anim);
				Test->TestEqual(TEXT("Shared player follows authored ownership"), Unified->IsPlayingOneShot(), bOverride);
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
					Report += FString::Printf(TEXT("Unified Scenario=%d Frame=%d Shot=%d Returning=%d OutgoingWeight=%.4f Players=%d ActualTime=%.4f Remaining=%.4f\n"),
						Scenario, Frame, Unified->IsPlayingOneShot(), Unified->IsReturning(), Unified->GetOutgoingWeight(),
						Unified->AnimPlayers.Num(), Unified->GetCurrentAssetTime(), Unified->GetOneShotSecondsRemaining());
				if (Frame == At(600) - 1) Test->TestFalse(TEXT("Completed shared returns release presentation"), Unified->IsReturning());
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
			if (Frame == At(600) - 1) Test->TestFalse(TEXT("Completed returns release their retained command"), Handoff->IsLiveReturnActive());
		}
		FVector Feet[2]; bool bFeet = !Player->GetMesh()->IsRunningParallelEvaluation();
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const int32 Bone = Player->GetMesh()->GetBoneIndex(Index ? TEXT("foot_r") : TEXT("foot_l"));
			bFeet &= Player->GetMesh()->GetComponentSpaceTransforms().IsValidIndex(Bone);
			if (bFeet) Feet[Index] = Player->GetMesh()->GetComponentSpaceTransforms()[Bone].GetLocation();
		}
		if (bReturn || bWindow || (bOverride && Frame % 10 == 0))
		{
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
		if (bFeet) { PreviousFoot[0] = Feet[0]; PreviousFoot[1] = Feet[1]; }
		bWasOverride = bOverride;
		if (++Frame < At(600)) return false;
		Test->TestTrue(FString::Printf(TEXT("Scenario %d actually returns from an authored external clip"), Scenario), Returns > 0);
		Test->TestTrue(TEXT("Return window evaluates actual bones"), WindowFrames >= 12);
		if (!bLand) Test->TestTrue(TEXT("Actually returns from Start, not merely Stop"), StartReturns > 0);
		else Test->TestTrue(TEXT("Actually returns from Land, not merely airborne loop"), LandReturns > 0);
		const auto* LiveCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.LiveOneShotReturn"));
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectOneShotHandoff")) && LiveCVar && LiveCVar->GetInt() != 0)
			Test->TestTrue(TEXT("Prepared graph overlaps outgoing and incoming poses"), LiveFrames >= 3);
		Report += FString::Printf(TEXT("Summary Scenario=%d Returns=%d WindowFrames=%d StartReturns=%d LandReturns=%d LiveFrames=%d\n"), Scenario, Returns, WindowFrames, StartReturns, LandReturns, LiveFrames);
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
			Test->TestEqual(FString::Printf(TEXT("Scenario %d yields once for sustained travel, never for transient/noise/camera-only"), Scenario), CurveReturns, CurveCases()[Scenario].Pattern == 0 ? 1 : 0);
			Report += FString::Printf(TEXT("CurveSummary Scenario=%d Pattern=%d Keys=(%.1f,%.1f) CurveReturns=%d\n"), Scenario, CurveCases()[Scenario].Pattern,
				CurveCases()[Scenario].Keys.X, CurveCases()[Scenario].Keys.Y, CurveReturns);
		}
		Cleanup(); Frame = Returns = WindowFrames = 0; ReturnFrame = -1000; bWasOverride = bPreviousFeet = false; OutgoingClip = NAME_None;
		PreviousUnifiedTime = -1.f; PreviousUnifiedSelection = INDEX_NONE;
		LiveFrames = StartReturns = LandReturns = 0;
		ShotFrame = -1; CompatibleFrames = ObservedTurnFrames = 0; CameraYaw = 0; bChangedDuringShot = false;
		CurveReturns = 0;
		if (++Scenario < (bDirectionCoverage ? 16 : bCurveResponse ? CurveCases().Num() : bInputResponse ? 16 : 8)) return false;
		FString Output; FParse::Value(FCommandLine::Get(), bDirectionCoverage ? TEXT("ProjectJOneShotDirectionOutput=") :
			bCurveResponse ? TEXT("ProjectJOneShotCurveOutput=") : bInputResponse ? TEXT("ProjectJOneShotInputOutput=") : TEXT("ProjectJOneShotReturnOutput="), Output);
		if (Output.IsEmpty()) Output = FPaths::ProjectSavedDir() / (bDirectionCoverage ? TEXT("Validation/OneShotReturn/DirectionPlayback.txt") :
			bCurveResponse ? TEXT("Validation/OneShotReturn/CurvePlayback.txt") :
			bInputResponse ? TEXT("Validation/OneShotReturn/InputPlayback.txt") : TEXT("Validation/OneShotReturn/Playback.txt"));
		Test->TestTrue(TEXT("Saved frame-by-frame return evidence"), FFileHelper::SaveStringToFile(Report, *Output));
		Test->AddInfo(TEXT("One-shot return evidence: ") + Output); return true;
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
#endif
