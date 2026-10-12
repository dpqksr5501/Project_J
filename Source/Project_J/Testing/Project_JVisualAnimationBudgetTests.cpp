#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RHI.h"

namespace
{
class FVisualBudgetLifecycle : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Local = nullptr;
	UProject_JAnimationUpdateCoordinatorComponent* Coordinator = nullptr;
	UProject_JBudgetedSkeletalMeshComponent* Source = nullptr;
	USkeletalMeshComponent* Body = nullptr;
	TStrongObjectPtr<UObject> GameplayRequester;
	int32 Frame = 0, FullUpdates = 0, FarUpdates = 0;
	int32 InterpolatedFrames = 0;
	TArray<FTransform> PreviousBones;
	USceneCaptureComponent2D* Capture = nullptr;
	void Cleanup()
	{
		if (!World) return;
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
	}
public:
	explicit FVisualBudgetLifecycle(FAutomationTestBase* InTest) : Test(InTest) {}
	~FVisualBudgetLifecycle() { Cleanup(); }
	bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
			World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(false);
			auto* Controller = World->SpawnActor<AAIController>(); auto* State = World->SpawnActor<AProject_JPlayerState>();
			State->SetOwner(Controller); Controller->SetPlayerState(State);
			Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0,0,1000));
			if (!Player) { Test->AddError(TEXT("Production visual fixture failed")); Cleanup(); return true; }
			Controller->UnPossess(); Player->SetPlayerState(State);
			Source = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh());
			Coordinator = Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
			TInlineComponentArray<USkeletalMeshComponent*> Visuals(Player);
			for (auto* Mesh : Visuals) if (Mesh != Source && Cast<UProject_JRetargetAnimInstance>(Mesh->GetAnimInstance())) { Body = Mesh; break; }
			GameplayRequester.Reset(NewObject<USceneComponent>(Player));
			if (!Test->TestNotNull(TEXT("Production visual body"), Body) || !Test->TestNotNull(TEXT("Native coordinator"), Coordinator)) { Cleanup(); return true; }
			if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderVisualBudget")))
			{
				if (!Test->TestFalse(TEXT("Visual budget capture uses a real RHI"), GUsingNullRHI)) { Cleanup(); return true; }
				auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
				Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(10000,10000,100));
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
				Floor->SetActorLocation(FVector(0,0,1000-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-100));
				auto* Surface = NewObject<UStaticMeshComponent>(Floor); Surface->SetupAttachment(Box);
				Surface->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Surface->SetRelativeScale3D(FVector(200,200,2)); Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision); Surface->RegisterComponent();
				auto* LightOwner = World->SpawnActor<AActor>(); auto* Light = NewObject<UDirectionalLightComponent>(LightOwner);
				LightOwner->SetRootComponent(Light); Light->SetMobility(EComponentMobility::Movable); Light->SetIntensity(5); Light->RegisterComponent();
				LightOwner->SetActorRotation(FRotator(-55,-135,0));
				auto* CaptureOwner = World->SpawnActor<AActor>(); Capture = NewObject<USceneCaptureComponent2D>(CaptureOwner); CaptureOwner->SetRootComponent(Capture);
				auto* Target = NewObject<UTextureRenderTarget2D>(Capture); Target->RenderTargetFormat = RTF_RGBA8;
				Target->InitCustomFormat(1024,768,PF_B8G8R8A8,false); Target->UpdateResourceImmediate();
				Capture->TextureTarget = Target; Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
				Capture->bCaptureEveryFrame = Capture->bCaptureOnMovement = false; Capture->RegisterComponent();
			}
		}
		const bool bHidden = Frame >= 360 && Frame < 420 || Frame >= 480 && Frame < 600;
		if (Frame == 480) Source->RequestAnimationUpdate(GameplayRequester.Get(), EProject_JAnimationUpdateRequirement::GameplayPose);
		if (Frame == 540) Source->ReleaseAnimationUpdate(GameplayRequester.Get());
		if (Frame == 600)
		{
			Local = World->SpawnActor<APlayerController>(); Local->SetAsLocalPlayerController(); Local->Possess(Player);
		}
		if (Frame == 660) { Local->UnPossess(); Player->SetPlayerState(nullptr); Coordinator->bAllowVisualBudget = false; }
		if (auto* Tier = FindFProperty<FFloatProperty>(AProject_JBaseCharacter::StaticClass(), TEXT("CurrentSignificance")))
			Tier->SetPropertyValue_InContainer(Player, Frame < 180 ? 0 : 2);
		Player->GetCharacterMovement()->Velocity = FVector(200,0,0); Player->AddActorWorldOffset(FVector(200.f/60,0,0));
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(bHidden ? -1000 : World->GetTimeSeconds());
		const int16 Before = Body->GetAnimInstance()->GetUpdateCounter().Get();
		World->Tick(LEVELTICK_All, 1.f / 60);
		const bool bUpdated = Body->GetAnimInstance()->GetUpdateCounter().Get() != Before;
		if (Frame >= 60 && Frame < 180) FullUpdates += bUpdated;
		if (Frame >= 240 && Frame < 360) FarUpdates += bUpdated;
		const auto& Bones = Body->GetComponentSpaceTransforms();
		if (Frame >= 240 && Frame < 360 && !bUpdated && PreviousBones.Num() == Bones.Num())
		{
			for (int32 Bone = 1; Bone < Bones.Num(); ++Bone)
				if (!Bones[Bone].Equals(PreviousBones[Bone], .001f)) { ++InterpolatedFrames; break; }
		}
		PreviousBones = Bones;
		if (Frame >= 380 && Frame < 420 || Frame >= 560 && Frame < 600)
		{
			Test->TestFalse(TEXT("Hidden visual group does not tick"), Body->IsComponentTickEnabled());
			Test->TestFalse(TEXT("Hidden visual body does not update its graph"), bUpdated);
		}
		if (Frame == 420 || Frame >= 480 && Frame < 540 || Frame >= 600 && Frame < 660)
		{
			Test->TestTrue(TEXT("Visibility wake, gameplay demand and local control immediately evaluate"), bUpdated);
			Test->TestTrue(TEXT("Protected visual group remains tick enabled"), Body->IsComponentTickEnabled());
		}
		if (Frame >= 480 && Frame < 540)
			Test->TestTrue(TEXT("Hidden gameplay demand retains source Hand IK"), CastChecked<UProject_JCharacterAnimInstance>(Source->GetAnimInstance())->GetCurrentOptimizationPolicy().bEnableHandIK);
		if (Frame >= 660) Test->TestFalse(TEXT("Actor opt-out restores external-control ownership"), Body->IsUsingExternalTickRateControl());
		if (Capture && (Frame >= 140 && Frame <= 146 || Frame >= 300 && Frame <= 306 || Frame == 420 || Frame == 480 || Frame == 620))
		{
			const FVector Center = Player->GetActorLocation() + FVector(0,0,-10), Position = Center + FVector(350,350,130);
			Capture->SetWorldLocationAndRotation(Position, (Center-Position).Rotation()); Capture->CaptureScene();
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/CrowdPresentation_20261012/Rendered/Frames");
			IFileManager::Get().MakeDirectory(*Directory, true);
			UKismetRenderingLibrary::ExportRenderTarget(World, Capture->TextureTarget, Directory, FString::Printf(TEXT("Frame%d.png"), Frame));
		}
		if (++Frame < 720) return false;
		Test->TestTrue(TEXT("Near full-rate body graph actually updates"), FullUpdates >= 110);
		Test->TestTrue(TEXT("Far body performs fewer real graph updates with interpolation"), FarUpdates > 0 && FarUpdates <= 45);
		Test->TestTrue(TEXT("Actual component-space bones advance on skipped graph-update frames"), InterpolatedFrames > 10);
		Test->AddInfo(FString::Printf(TEXT("Visual body over 120 ticks NearUpdates=%d FarUpdates=%d SkippedFramesWithBoneInterpolation=%d"), FullUpdates, FarUpdates, InterpolatedFrames));
		Cleanup(); return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualBudgetLifecycleTest, "ProjectJ.Animation.VisualBudget.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualBudgetLifecycleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FVisualBudgetLifecycle(this)); return true; }
#endif
