#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "SignificanceManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationObserverRuntime, "ProjectJ.Animation.Budget.ObserverRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationObserverRuntime::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    auto* Character = World->SpawnActor<AProject_JNPCCharacter>();
    Character->SetActorLocation(FVector(30000, 0, 0));
    Character->SetActorTickEnabled(false);
    Character->GetCharacterMovement()->SetComponentTickEnabled(false);
    auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Character->GetMesh());
    auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
    Anim->NativeInitializeAnimation();
    auto* Service = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
    Service->SetEnabledOverride(false); // Observation must work without ABA ownership.
    auto* Viewer = World->SpawnActor<APlayerController>();
    Viewer->SetAsLocalPlayerController();
    auto* Camera = World->SpawnActor<ACameraActor>();
    Viewer->SetViewTarget(Camera);
    if (!TestNotNull(TEXT("Local view has a camera manager"), Viewer->PlayerCameraManager.Get())) return false;
    Viewer->PlayerCameraManager->UpdateCamera(0.f);
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        Mesh->SetLastRenderTime(World->GetTimeSeconds());
        World->Tick(LEVELTICK_All, 1.f / 60);
    }
    Mesh->SetLastRenderTime(World->GetTimeSeconds());
    const float OriginalGlobalSignificance = Character->GetSignificance();
    const auto Far = Anim->BuildOptimizationPolicy();
    const auto* Manager = USignificanceManager::Get(World);
    AddInfo(FString::Printf(TEXT("ObserverRuntime LocalViews=1 Distance=30000 LegacyViewpoints=%d GlobalSignificance=%.1f AnimationTier=%d"),
        Manager ? Manager->GetViewpoints().Num() : -1, OriginalGlobalSignificance, int32(Far.Tier)));
    TestEqual(TEXT("Visible far character uses animation Far without manually injected significance"), Far.Tier, EProject_JAnimBudgetTier::Far);
    TestTrue(TEXT("Far still evaluates a visible body"), Far.bEnableFollowerRetarget);
    Mesh->RequestAnimationUpdate(Viewer, EProject_JAnimationUpdateRequirement::GameplayPose);
    TestTrue(TEXT("Gameplay pose retains hand and foot quality at any observer distance"),
        Anim->BuildOptimizationPolicy().bEnableHandIK && Anim->BuildOptimizationPolicy().bEnableFootIK);
    Mesh->ReleaseAnimationUpdate(Viewer);
    Camera->SetActorLocation(Character->GetActorLocation());
    Viewer->PlayerCameraManager->UpdateCamera(0.f);
    Service->Refresh();
    TestEqual(TEXT("Moving the actual view near restores Near"), Anim->BuildOptimizationPolicy().Tier, EProject_JAnimBudgetTier::Near);
    Camera->SetActorLocation(FVector::ZeroVector);
    Viewer->PlayerCameraManager->UpdateCamera(0.f);
    Service->Refresh();
    TestEqual(TEXT("The view can move far again"), Anim->BuildOptimizationPolicy().Tier, EProject_JAnimBudgetTier::Far);
    auto* SecondViewer = World->SpawnActor<APlayerController>();
    SecondViewer->SetAsLocalPlayerController();
    auto* SecondCamera = World->SpawnActor<ACameraActor>();
    SecondCamera->SetActorLocation(Character->GetActorLocation());
    SecondViewer->SetViewTarget(SecondCamera);
    SecondViewer->PlayerCameraManager->UpdateCamera(0.f);
    Service->Refresh();
    TestEqual(TEXT("Nearest local view wins when another local view is far"), Anim->BuildOptimizationPolicy().Tier, EProject_JAnimBudgetTier::Near);
    SecondViewer->Destroy(); SecondCamera->Destroy();
    Service->Refresh();
    TestEqual(TEXT("Removing the near view restores the remaining far view"), Anim->BuildOptimizationPolicy().Tier, EProject_JAnimBudgetTier::Far);
    Viewer->Possess(Character);
    TestEqual(TEXT("Local distance protection is immediate before the next observation pass"), Mesh->GetAnimationDistanceTier(), 0.f);
    TestEqual(TEXT("Local control keeps full quality regardless of camera distance"), Anim->BuildOptimizationPolicy().Tier, EProject_JAnimBudgetTier::Local);
    TestEqual(TEXT("Animation observation does not write the shared AI/BP significance"), Character->GetSignificance(), OriginalGlobalSignificance);
    Viewer->UnPossess();
    Viewer->Destroy(); Camera->Destroy();
    Service->Refresh();
    TestEqual(TEXT("No view clears the old observation and restores the existing tier"), Mesh->GetAnimationDistanceTier(), OriginalGlobalSignificance);
    if (auto* LegacyTier = FindFProperty<FFloatProperty>(AProject_JBaseCharacter::StaticClass(), TEXT("CurrentSignificance")))
    {
        LegacyTier->SetPropertyValue_InContainer(Character, 2.f);
        Service->Refresh();
        TestEqual(TEXT("No-view fixtures retain explicitly injected legacy tiers"), Mesh->GetAnimationDistanceTier(), 2.f);
        LegacyTier->SetPropertyValue_InContainer(Character, OriginalGlobalSignificance);
    }
    Service->UnregisterMesh(Mesh);
    TestEqual(TEXT("Unregister cannot retain an observer value"), Mesh->GetAnimationDistanceTier(), OriginalGlobalSignificance);
    Service->RegisterMesh(Mesh);
    Anim->NativeUninitializeAnimation();
    return true;
}
#endif
