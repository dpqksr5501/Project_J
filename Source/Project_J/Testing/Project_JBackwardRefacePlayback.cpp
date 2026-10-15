#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Engine/Engine.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"

namespace
{
class FBackwardRefacePlayback : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	USceneCaptureComponent2D* Capture = nullptr;
	int32 Case = 0, Frame = 0, OpenFrames = 0, SelectedFrames = 0, LateFrames = 0;
	bool bEnabled = false, bRender = false, bRenderSequence = false;
	FString Output, Report;
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
		Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10000,10000,100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-100));
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		Controller = World->SpawnActor<APlayerController>(); Controller->SetAsLocalPlayerController();
		auto* State = World->SpawnActor<AProject_JPlayerState>(); State->SetOwner(Controller); Controller->SetPlayerState(State);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World,Controller,State,FVector(0,0,110));
		if (!Player) return false;
		Player->SetActorEnableCollision(true); Player->GetCharacterMovement()->SetComponentTickEnabled(true);
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		const auto* Set = Player->GetCombatStrafeMotionMatchingAssetSet();
		if (!Set || !Set->RunDatabases.Cycle) return false;
		if (bEnabled && !Test->TestNotNull(TEXT("Installed Combat Run has authored backward Reface data"),Set->RunDatabases.BackwardReface.Get())) return false;
		if (bRender && Case < 2)
		{
			auto* VisualFloor = NewObject<UStaticMeshComponent>(Floor); VisualFloor->SetupAttachment(Box);
			VisualFloor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
			VisualFloor->SetRelativeScale3D(FVector(200,200,2)); VisualFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision); VisualFloor->RegisterComponent();
			auto* LightActor = World->SpawnActor<AActor>(); auto* Light = NewObject<UDirectionalLightComponent>(LightActor);
			LightActor->SetRootComponent(Light); Light->SetMobility(EComponentMobility::Movable); Light->SetIntensity(5); Light->RegisterComponent();
			LightActor->SetActorRotation(FRotator(-55,-35,0));
			auto* CaptureActor = World->SpawnActor<AActor>(); Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
			CaptureActor->SetRootComponent(Capture);
			auto* Target = NewObject<UTextureRenderTarget2D>(Capture); Target->RenderTargetFormat = RTF_RGBA8;
			Target->InitCustomFormat(1024,768,PF_B8G8R8A8,false);
			Target->ClearColor = FLinearColor(.08f,.08f,.08f,1); Target->UpdateResourceImmediate();
			Capture->TextureTarget = Target; Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Capture->bCaptureEveryFrame = false; Capture->bCaptureOnMovement = false; Capture->FOVAngle = 50; Capture->RegisterComponent();
		}
		return true;
	}
public:
	explicit FBackwardRefacePlayback(FAutomationTestBase* InTest) : Test(InTest)
	{
		FParse::Value(FCommandLine::Get(),TEXT("ProjectJRefacePlaybackOutput="),Output);
		if (Output.IsEmpty()) Output = FPaths::ProjectSavedDir()/TEXT("Validation/BackwardReface/Playback.txt");
		bRender = FParse::Param(FCommandLine::Get(),TEXT("ProjectJRefaceRender"));
		bRenderSequence = FParse::Param(FCommandLine::Get(),TEXT("ProjectJRefaceRenderSequence"));
		const auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.BackwardRefaceCandidates"));
		bEnabled = CVar && CVar->GetInt()!=0;
		Report = FString::Printf(TEXT("Actual CMC + installed primary Master/follower. Enabled=%d; render is opt-in; no asset writes.\n"),bEnabled);
	}
	virtual ~FBackwardRefacePlayback() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Backward Reface fixture setup failed")); Cleanup(); return true; }
		constexpr float Dt = 1.f/120;
		float Rate = Case < 2 ? 900 : Case < 4 ? 450 : Case < 6 ? 60 : 900;
		if (Case%2) Rate = -Rate;
		const float Sweep = Case < 6 && Case >= 4 ? 90 : 180;
		const float Camera = Frame > 180 ? FMath::Sign(Rate)*FMath::Min(Sweep,(Frame-180)*Dt*FMath::Abs(Rate)) : 0;
		const float Offset = Case == 6 ? -90 : Case == 7 ? 90 : Case == 8 ? 0 : 180;
		Controller->SetControlRotation(FRotator(0,Camera,0));
		const bool bRelease = Case == 9 && Frame >= 196;
		if (bRelease) Player->GetLocomotionAnimStateComponent()->ClearMoveInput();
		else
		{
			const float Radians = FMath::DegreesToRadians(Offset);
			Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(FMath::Sin(Radians),FMath::Cos(Radians)));
			Player->AddMovementInput(FRotator(0,Camera+Offset,0).Vector());
		}
		if (Case == 10) Player->bIsDodging = Frame >= 196 && Frame < 240;
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		if (Case == 11 && Frame == 196)
		{
			FGameplayTagContainer Tags; Player->GetAbilitySystemComponent()->GetOwnedGameplayTags(Tags);
			const auto* Style = Player->GetCombatStyleDefinition();
			const auto* Entry = Style && Style->ComboDefinition ? Style->ComboDefinition->FindStartNode(FProject_JGameplayTags::Get().InputTag_Weapon_LMB,Tags) : nullptr;
			auto* Montage = Entry && Entry->AttackDefinition ? Entry->AttackDefinition->Montage.Get() : nullptr;
			if (!Test->TestTrue(TEXT("Real equipped montage begins during correction"),Montage && Anim->Montage_Play(Montage)>0)) { Cleanup(); return true; }
		}
		if (Case == 12 && Frame == 196) Player->GetAbilitySystemComponent()->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		World->Tick(LEVELTICK_All,Dt); ++GFrameCounter;
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const auto& Feedback = Anim->GetCompletedTurnFeedback();
		const bool bOpen = MM.SelectionContext.bBackwardRefaceCandidates;
		const bool bSelected = MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("BackwardReface"));
		OpenFrames += bOpen; SelectedFrames += bSelected;
		if(Frame>=330) LateFrames += bOpen || MM.SelectionContext.bAllowBackwardRefaceContinuation || bSelected;
		if (Frame>=120)
		{
			Report += FString::Printf(TEXT("RefaceFrame Case=%d Frame=%d Camera=%.2f Actor=%.2f Visual=%.2f VisualValid=%d Speed=%.2f Velocity=%.2f Open=%d Continue=%d PSD=%s Clip=%s Time=%.3f Cost=%.4f Continuing=%d Override=%d Phase=%d\n"),
				Case,Frame,Camera,Player->GetActorRotation().Yaw,Feedback.VisualFacingYaw,Feedback.bVisualFacingValid,
				Player->GetVelocity().Size2D(),Player->GetVelocity().Rotation().Yaw,bOpen,MM.SelectionContext.bAllowBackwardRefaceContinuation,
				*MM.PostSelection.SelectedDatabase.ToString(),*MM.PostSelection.SelectedAnimation.ToString(),MM.PostSelection.SelectedAnimationTime,MM.PostSelection.SearchCost,
				MM.PostSelection.bIsContinuingPoseSearch,Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching(),int32(MM.SelectionContext.PhaseFamily));
		}
		if (Frame>=120 && Capture)
		{
			TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
			for(auto* Mesh:Meshes)
			{
				if(!Mesh->GetSkeletalMeshAsset() || Mesh->GetBoneIndex(TEXT("foot_l"))<0 || Mesh->GetBoneIndex(TEXT("foot_r"))<0) continue;
				const FVector L=Mesh->GetBoneLocation(TEXT("foot_l")), R=Mesh->GetBoneLocation(TEXT("foot_r"));
				const FVector Root=Mesh->GetBoneLocation(TEXT("root"),EBoneSpaces::ComponentSpace);
				const FVector Actor=Player->GetActorLocation();
				Report+=FString::Printf(TEXT("RefaceFeet Case=%d Frame=%d Mesh=%s Primary=%d Visible=%d Actor=(%.3f,%.3f,%.3f) RootCS=(%.3f,%.3f,%.3f) L=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f)\n"),
					Case,Frame,*Mesh->GetName(),Mesh==Player->GetMesh(),Mesh->IsVisible(),Actor.X,Actor.Y,Actor.Z,Root.X,Root.Y,Root.Z,L.X,L.Y,L.Z,R.X,R.Y,R.Z);
			}
		}
		if ((Offset!=180 || bRelease || Player->bIsDodging || Anim->IsAnyMontagePlaying() || (Case == 12 && Frame > 196) ||
			Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching()) && Frame>120)
			Test->TestFalse(TEXT("Direction/action/one-shot owner excludes backward Reface"),bOpen || MM.SelectionContext.bAllowBackwardRefaceContinuation);
		const bool bSequenceFrame=bRenderSequence && Frame>=174 && Frame<=348 && Frame%2==0;
		if (Capture && Case < 2 && (bSequenceFrame || Frame==174 || Frame==186 || Frame==198 || Frame==210 || Frame==234 || Frame==258 || Frame==300 || Frame==348))
		{
			const FVector Center = Player->GetActorLocation()+FVector(0,0,-10);
			const FVector Position = Center+FVector(350,350,130);
			Capture->SetWorldLocationAndRotation(Position,(Center-Position).Rotation()); Capture->CaptureScene();
			const FString Directory=FPaths::GetPath(Output)/TEXT("Frames"); IFileManager::Get().MakeDirectory(*Directory,true);
			UKismetRenderingLibrary::ExportRenderTarget(World,Capture->TextureTarget,Directory,FString::Printf(TEXT("Case%d_Frame%d.png"),Case,Frame));
		}
		if(++Frame<420) return false;
		Report += FString::Printf(TEXT("RefaceSummary Case=%d OpenFrames=%d SelectedFrames=%d LateFrames=%d\n"),Case,OpenFrames,SelectedFrames,LateFrames);
		if (bEnabled && Case<2) Test->TestTrue(TEXT("A fast held-S reversal supplies authored Reface candidates"),OpenFrames>0);
		if (bEnabled && Case<2) Test->TestTrue(TEXT("Original Reface wins actual competitive searches in both directions"),SelectedFrames>0);
		if (Case==4 || Case==5 || Case==6 || Case==7 || Case==8) Test->TestEqual(TEXT("Slow S curves and A/D/W retain their original candidates"),OpenFrames,0);
		Test->TestEqual(TEXT("Correction returns to Cycle without stale Reface debt"),LateFrames,0);
		Cleanup(); Frame=OpenFrames=SelectedFrames=LateFrames=0;
		if(++Case<13) return false;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
		Test->TestTrue(TEXT("Saved frame-aligned Reface evidence"),FFileHelper::SaveStringToFile(Report,*Output));
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBackwardRefacePlaybackTest,"ProjectJ.Animation.Naturalness.BackwardRefaceCMC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBackwardRefacePlaybackTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FBackwardRefacePlayback(this)); return true;
}
#endif
