#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJComboBlendOutTest, "ProjectJ.Combat.ComboBlendOutWindows",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJComboBlendOutTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 const auto Cleanup = [&]
 {
  World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
  World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 };
 FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
 auto* Controller = World->SpawnActor<AAIController>(Params);
 auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
 if (!Controller || !State) { AddError(TEXT("Failed to create combo fixture")); Cleanup(); return false; }
 State->SetOwner(Controller); Controller->SetPlayerState(State);
 auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 1000));
 if (!TestNotNull(TEXT("Authored greatsword player"), Player)) { Cleanup(); return false; }
 auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent());
 const auto& Tags = FProject_JGameplayTags::Get();
 ASC->AddLooseGameplayTag(Tags.State_CombatMode);
 Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 Player->GetMesh()->bEnableUpdateRateOptimizations = false;
 const auto Tick = [&] { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f / 60.f); };
 for (int32 Frame = 0; Frame < 5; ++Frame) Tick();

 struct FRoute { const TCHAR* Expected; TArray<FGameplayTag> Inputs; TArray<int32> Frames; int32 ObserveFrame; };
 const TArray<FRoute> Routes = {
  { TEXT("Combo.Greatsword.LMB.2"), {Tags.InputTag_Weapon_LMB, Tags.InputTag_Weapon_LMB}, {0, 15}, 100 },
  { TEXT("Combo.Greatsword.RMB.2"), {Tags.InputTag_Weapon_RMB, Tags.InputTag_Weapon_RMB}, {0, 15}, 100 },
  { TEXT("Combo.Greatsword.Q.2"), {Tags.InputTag_Skill_Q, Tags.InputTag_Skill_Q}, {0, 15}, 110 },
  { TEXT("Combo.Greatsword.Q.2"), {Tags.InputTag_Weapon_LMB, Tags.InputTag_Weapon_RMB,
    Tags.InputTag_Weapon_RMB, Tags.InputTag_Weapon_RMB}, {0, 15, 72, 118}, 210 }
 };
 for (const FRoute& Route : Routes)
 {
  UProject_JGameplayAbility_Melee* Ability = nullptr;
  int32 Input = 0;
  for (int32 Frame = 0; Frame <= Route.ObserveFrame; ++Frame)
  {
   if (Input < Route.Frames.Num() && Frame == Route.Frames[Input])
   {
    Player->HandleSkillInputTagPressed(Route.Inputs[Input]);
    Player->HandleSkillInputTagReleased(Route.Inputs[Input++]);
    for (const auto& Spec : ASC->GetActivatableAbilities())
     if (Spec.IsActive()) if (auto* Melee = Cast<UProject_JGameplayAbility_Melee>(Spec.GetPrimaryInstance())) Ability = Melee;
   }
   Tick();
  }
  if (TestNotNull(TEXT("Actual granted combo ability"), Ability))
  {
   TestTrue(TEXT("Combo remains active after its authored window"), Ability->IsActive());
   TestEqual(TEXT("Buffered inputs traverse real authored montage notifies"), Ability->CurrentComboNodeTag.ToString(), FString(Route.Expected));
   ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
   TestFalse(TEXT("Cancellation retires combo windows"), Ability->bIsComboWindowOpen);
   TestNull(TEXT("Cancellation releases montage ownership"), ASC->GetAnimatingAbility());
  }
  for (int32 Frame = 0; Frame < 30; ++Frame) Tick();
 }
 Cleanup(); return true;
}
#endif
