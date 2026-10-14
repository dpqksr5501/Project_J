#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SceneComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationDemandLifetimeTest, "ProjectJ.Animation.DemandOwnerLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationDemandLifetimeTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 auto* Actor = World->SpawnActor<AProject_JNPCCharacter>();
 auto* Mesh = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Actor->GetMesh());
 auto* Service = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
 Mesh->bEnableUpdateRateOptimizations = true;
 Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
 auto* GameplayOwner = NewObject<USceneComponent>(Actor);
 auto* VisualOwner = NewObject<USceneComponent>(Actor);
 Mesh->RequestAnimationUpdate(GameplayOwner, EProject_JAnimationUpdateRequirement::GameplayPose);
 Mesh->RequestAnimationUpdate(VisualOwner, EProject_JAnimationUpdateRequirement::Presentation);
 GameplayOwner->MarkAsGarbage();
 // No new request/release and no montage boundary: the existing policy pass
 // must retire expired weak owners without releasing another owner's demand.
 Service->Refresh();
 TestTrue(TEXT("Live presentation demand remains"), Mesh->HasAnimationUpdateRequirement());
 TestFalse(TEXT("Expired gameplay owner cannot retain combat-critical pose"), Mesh->IsCombatCritical());
 TestEqual(TEXT("Expired gameplay demand restores visibility"), Mesh->VisibilityBasedAnimTickOption,
  EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
 VisualOwner->MarkAsGarbage();
 Service->Refresh();
 TestFalse(TEXT("No expired owner can retain a full-rate override"), Mesh->HasAnimationUpdateRequirement());
 TestTrue(TEXT("Last demand expiration restores URO"), Mesh->bEnableUpdateRateOptimizations);
 World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
