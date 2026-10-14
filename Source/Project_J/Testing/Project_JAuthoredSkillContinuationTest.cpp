#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredSkillContinuationTest, "ProjectJ.Combat.AuthoredSkillContinuation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredSkillContinuationTest::RunTest(const FString&)
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
 if (!Controller || !State) { AddError(TEXT("Fixture owner creation failed")); Cleanup(); return false; }
 State->SetOwner(Controller); Controller->SetPlayerState(State);
 auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 1000));
 if (!TestNotNull(TEXT("Actual greatsword BP"), Player)) { Cleanup(); return false; }
 auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent());
 // AIController permits Standalone predicted GAS, but does not execute
 // SetupPlayerInputComponent. Perform its normal router ownership binding.
 auto* Router = Player->FindComponentByClass<UProject_JSkillInputRouterComponent>();
 if (!TestNotNull(TEXT("Authored input router"), Router)) { Cleanup(); return false; }
 Router->Initialize(Player);
 const auto& Tags = FProject_JGameplayTags::Get();
 ASC->AddLooseGameplayTag(Tags.State_CombatMode);
 Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 Player->GetMesh()->bEnableUpdateRateOptimizations = false;
 float Hz = 60.f;
 FParse::Value(FCommandLine::Get(), TEXT("ProjectJSkillContinuationHz="), Hz);
 if (!FMath::IsFinite(Hz) || Hz < 15 || Hz > 240) { AddError(TEXT("Invalid fixture frequency")); Cleanup(); return false; }
 const auto Frames = [&](float Seconds) { return FMath::CeilToInt(Seconds * Hz); };
 const auto Tick = [&] { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f / Hz); };
 bool bMouseTap = false;
 const auto Press = [&](FGameplayTag Tag)
 {
  if (bMouseTap && Tag == Tags.InputTag_Weapon_RMB)
  {
   Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
   Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
  }
  else { Player->HandleSkillInputTagPressed(Tag); Player->HandleSkillInputTagReleased(Tag); }
 };
 const auto GetActive = [&]() -> UProject_JGameplayAbility_Melee*
 {
  for (const auto& Spec : ASC->GetActivatableAbilities())
   if (Spec.IsActive()) if (auto* Ability = Cast<UProject_JGameplayAbility_Melee>(Spec.GetPrimaryInstance())) return Ability;
  return nullptr;
 };
 for (int32 Frame = 0; Frame < 5; ++Frame) Tick();
 const auto* Style = Player->GetCombatStyleDefinition();
 const auto* Combo = Style ? Style->ComboDefinition.Get() : nullptr;
 if (!TestNotNull(TEXT("Actual equipped combo definition"), Combo)) { Cleanup(); return false; }
 const auto* RNode = Combo->FindStartNode(Tags.InputTag_Skill_R, FGameplayTagContainer());
 if (!TestNotNull(TEXT("Authored R entry"), RNode)) { Cleanup(); return false; }
 const auto* Edge = Combo->FindTransition(*RNode, Tags.InputTag_Weapon_RMB, FGameplayTagContainer());
 if (!TestNotNull(TEXT("Authored R to RMB transition"), Edge)) { Cleanup(); return false; }
 const auto* Montage = RNode->AttackDefinition->Montage.Get();
 for (const auto& Notify : Montage->Notifies)
  AddInfo(FString::Printf(TEXT("R notify=%s time=%.4f duration=%.4f"), *GetNameSafe(Notify.NotifyStateClass), Notify.GetTime(), Notify.GetDuration()));

 // Early buffered and late window inputs use the production execution component,
 // granted ability, montage task, and native authored notifies. No synthetic window.
 for (const bool bThroughMouse : {false, true})
 for (const float InputAt : {.25f, 4.f, 4.25f})
 {
  const int32 InputFrame = Frames(InputAt);
  bMouseTap = bThroughMouse;
  Press(Tags.InputTag_Skill_R);
  bool bObservedTarget = false;
  FString LastState;
  for (int32 Frame = 0; Frame < Frames(500.f / 60.f); ++Frame)
  {
   if (Frame == InputFrame) Press(Tags.InputTag_Weapon_RMB);
   Tick();
   auto* Ability = GetActive();
   if (Ability && Ability->CurrentComboNodeTag == Edge->TargetNodeTag) bObservedTarget = true;
   const FString StateText = FString::Printf(TEXT("node=%s active=%d window=%d queued=%d montage=%s attacking=%d"),
    Ability ? *Ability->CurrentComboNodeTag.ToString() : TEXT("None"), Ability != nullptr,
    Ability && Ability->bIsComboWindowOpen, Ability && Ability->bHasNextComboQueued,
    *GetNameSafe(ASC->GetCurrentMontage()), ASC->HasMatchingGameplayTag(Tags.State_Attacking));
   if (StateText != LastState) { AddInfo(FString::Printf(TEXT("MouseTap=%d InputFrame=%d frame=%d %s"), bMouseTap, InputFrame, Frame, *StateText)); LastState = StateText; }
  }
  TestTrue(FString::Printf(TEXT("MouseTap=%d R RMB input at frame %d traverses authored target"), bMouseTap, InputFrame), bObservedTarget);
  TestNull(TEXT("Natural completion retires ability"), GetActive());
  TestFalse(TEXT("Natural completion releases attack ownership"), ASC->HasMatchingGameplayTag(Tags.State_Attacking));
  if (auto* Ability = GetActive()) ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
  for (int32 Frame = 0; Frame < Frames(.5f); ++Frame) Tick();
 }
 Press(Tags.InputTag_Skill_R);
 for (int32 Frame = 0; Frame < Frames(5.5f); ++Frame) Tick();
 TestNull(TEXT("R alone completes naturally"), GetActive());
 Press(Tags.InputTag_Weapon_RMB);
 for (int32 Frame = 0; Frame < Frames(.25f); ++Frame) Tick();
 Press(Tags.InputTag_Weapon_RMB);
 bool bObservedRMB2 = false;
 for (int32 Frame = 0; Frame < Frames(140.f / 60.f); ++Frame)
 {
  Tick(); if (auto* Ability = GetActive()) bObservedRMB2 |= Ability->CurrentComboNodeTag.ToString() == TEXT("Combo.Greatsword.RMB.2");
 }
 TestTrue(TEXT("R natural end permits a fresh RMB combo"), bObservedRMB2);
 if (auto* Ability = GetActive()) ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
 // A cancelled buffered follow-up owns neither a later window nor the next grant.
 Press(Tags.InputTag_Skill_R);
 for (int32 Frame = 0; Frame < Frames(.25f); ++Frame) Tick();
 Press(Tags.InputTag_Weapon_RMB);
 State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
 for (int32 Frame = 0; Frame < Frames(5.f); ++Frame) Tick();
 TestNull(TEXT("Unequip retires the buffered skill"), GetActive());
 TestFalse(TEXT("Unequip releases attack tag"), ASC->HasMatchingGameplayTag(Tags.State_Attacking));
 auto* Item = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
 State->GetEquipmentManagerComponent()->EquipItem(Item);
 Press(Tags.InputTag_Skill_R);
 auto* NewAttack = GetActive();
 TestTrue(TEXT("Re-equip starts an independent authored entry"), NewAttack && NewAttack->CurrentComboNodeTag == RNode->NodeTag);
 if (NewAttack) ASC->CancelAbilityHandle(NewAttack->GetCurrentAbilitySpecHandle());
 Cleanup(); return true;
}
#endif
