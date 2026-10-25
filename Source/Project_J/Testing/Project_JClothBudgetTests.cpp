#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "DynamicRHI.h"

namespace
{
UWorld* ClothTestWorld()
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    return World;
}
void EndClothTestWorld(UWorld* World)
{
    World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
}
AProject_JPlayerCharacter* ClothTestPawn(UWorld* World,const FVector& Position)
{
    auto* Controller=World->SpawnActor<AAIController>();
    auto* State=World->SpawnActor<AProject_JPlayerState>();
    Controller->SetPlayerState(State);State->SetOwner(Controller);
    return ProjectJAuthoredAnimationFixture::Spawn(World,Controller,State,Position);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJClothAllocationTest,"ProjectJ.Animation.ClothPolicy.Allocation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJClothAllocationTest::RunTest(const FString&)
{
    auto* World=ClothTestWorld();ON_SCOPE_EXIT{EndClothTestWorld(World);};
    auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.ClothBudget.MaxFullRemoteParts"));
    const int32 Previous=Limit->GetInt();Limit->Set(1,ECVF_SetByCode);
    ON_SCOPE_EXIT{Limit->Set(Previous,ECVF_SetByCode);};
    auto* Viewer=World->SpawnActor<APlayerController>();Viewer->SetAsLocalPlayerController();
    auto* Camera=World->SpawnActor<ACameraActor>();Viewer->SetViewTarget(Camera);Viewer->PlayerCameraManager->UpdateCamera(0);
    TArray<AProject_JPlayerCharacter*> Players;
    for(int32 Index=0;Index<3;++Index)
    {
        auto* Player=ClothTestPawn(World,FVector(100.f+100.f*Index,0,0));
        if(!TestNotNull(TEXT("Production pawn exists"),Player))return false;
        auto* Coordinator=Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
        // Allocation is a value/demand test, independent of a GPU simulation.
        Coordinator->RestoreClothBudget();Coordinator->ClothMeshes.AddDefaulted();
        Players.Add(Player);
    }
    Viewer->Possess(Players[2]);Viewer->SetViewTarget(Camera);Viewer->PlayerCameraManager->UpdateCamera(0);
    auto* Service=World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();Service->SetEnabledOverride(false);Service->Refresh();
    auto Source=[&](int32 Index){return CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Players[Index]->GetMesh());};
    TestTrue(TEXT("Nearest remote wins the one-part quota even with ABA disabled"),Source(0)->HasFullClothQualityGrant());
    TestFalse(TEXT("Other remote is budgeted independently of gameplay pose"),Source(1)->HasFullClothQualityGrant());
    TestTrue(TEXT("Local quality is outside the remote quota"),Source(2)->HasFullClothQualityGrant());
    Players[1]->SetActorLocation(FVector(95,0,0));Service->Refresh();
    TestTrue(TEXT("Small distance crossing retains the current grant"),Source(0)->HasFullClothQualityGrant());
    Players[1]->SetActorLocation(FVector(20,0,0));Service->Refresh();
    TestTrue(TEXT("Substantially nearer remote can take the grant"),Source(1)->HasFullClothQualityGrant());
    Limit->Set(0,ECVF_SetByCode);Service->Refresh();
    TestFalse(TEXT("Zero remote quota reduces all remotes"),Source(1)->HasFullClothQualityGrant());
    TestTrue(TEXT("Zero remote quota still preserves the local player"),Source(2)->HasFullClothQualityGrant());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJClothLifecycleTest,"ProjectJ.Animation.ClothRuntime.InteractorLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FProjectJClothLifecycleTest::RunTest(const FString&)
{
    if(GUsingNullRHI){AddError(TEXT("This interactor lifecycle test requires real RHI, not an absent simulation."));return false;}
    auto* World=ClothTestWorld();ON_SCOPE_EXIT{EndClothTestWorld(World);};
    auto* Player=ClothTestPawn(World,FVector(0,0,1000));
    if(!TestNotNull(TEXT("Production pawn exists"),Player))return false;
    TInlineComponentArray<UProject_JModularMeshComponent*> Parts(Player);
    UProject_JModularMeshComponent* Cape=nullptr;
    for(auto* Part:Parts)if(Part->GetSkeletalMeshAsset()->HasActiveClothingAssets()){Cape=Part;break;}
    if(!TestNotNull(TEXT("Real cloth garment exists"),Cape))return false;
    World->bShouldSimulatePhysics=true;
    Cape->RecreateClothingActors();
    auto* Coordinator=Player->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
    Coordinator->DiscoverVisualMeshes();
    if(!TestEqual(TEXT("Actual garment supports an instance binding"),Coordinator->GetManagedClothCount(),1))return false;
    auto* Source=CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh());
    auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.ClothBudget.MaxFullRemoteParts"));
    const int32 Previous=Limit->GetInt();Limit->Set(0,ECVF_SetByCode);
    ON_SCOPE_EXIT{Limit->Set(Previous,ECVF_SetByCode);};
    auto* Viewer=World->SpawnActor<APlayerController>();Viewer->SetAsLocalPlayerController();
    auto* Camera=World->SpawnActor<ACameraActor>();Viewer->SetViewTarget(Camera);Viewer->PlayerCameraManager->UpdateCamera(0);
    World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->Refresh();
    World->Tick(LEVELTICK_All,.35f);
    Coordinator->ClothDowngradeSince=World->GetTimeSeconds()-.3;
    Coordinator->RefreshClothBudget(Source,false);
    TestEqual(TEXT("Reduced instance command accepted"),Coordinator->GetReducedClothCount(),1);
    // This transient world exercises command/identity ownership. The rendered
    // crowd fixture records applied solver values after actual visible ticks.
    Coordinator->RefreshClothBudget(Source,true);
    TestEqual(TEXT("Local ownership immediately restores authored quality"),Coordinator->GetReducedClothCount(),0);
    auto* Original=Cape->GetSkeletalMeshAsset();
    Coordinator->ClothDowngradeSince=World->GetTimeSeconds()-.3;
    Coordinator->RefreshClothBudget(Source,false);
    Cape->SetSkeletalMeshAsset(Player->GetMesh()->GetSkeletalMeshAsset());
    Coordinator->RefreshClothBudget(Source,false);
    TestEqual(TEXT("Mesh replacement releases the old simulation binding"),Coordinator->GetManagedClothCount(),0);
    Cape->SetSkeletalMeshAsset(Original);
    Coordinator->DiscoverVisualMeshes();
    TestEqual(TEXT("Garment can bind after replacement"),Coordinator->GetManagedClothCount(),1);
    Cape->bAllowClothQualityBudget=false;
    Coordinator->RefreshClothBudget(Source,false);
    TestEqual(TEXT("Garment opt-out releases ownership"),Coordinator->GetManagedClothCount(),0);
    Cape->bAllowClothQualityBudget=true;
    Coordinator->DiscoverVisualMeshes();
    Viewer->Possess(Player);
    Coordinator->RefreshVisualBudget(0);
    TestEqual(TEXT("Actual local possession retains full cloth"),Coordinator->GetReducedClothCount(),0);
    Coordinator->RestoreClothBudget();
    TestEqual(TEXT("Teardown releases bindings"),Coordinator->GetManagedClothCount(),0);
    return true;
}
#endif
