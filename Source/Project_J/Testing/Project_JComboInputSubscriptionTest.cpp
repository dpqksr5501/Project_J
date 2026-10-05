#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJComboInputSubscriptionTest, "ProjectJ.Combat.ComboInputSubscription",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJComboInputSubscriptionTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
 auto* Controller = World->SpawnActor<AAIController>(Params);
 auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
 const auto Cleanup = [&]
 {
  World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
  World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 };
 if (!TestNotNull(TEXT("Fixture controller"), Controller) || !TestNotNull(TEXT("Fixture PlayerState"), State))
 { Cleanup(); return false; }
 State->SetOwner(Controller); Controller->SetPlayerState(State);
 auto* Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 1000));
 if (!TestNotNull(TEXT("Real authored player for input subscription"), Player)) { Cleanup(); return false; }
 auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent());
 const auto& Tags = FProject_JGameplayTags::Get();
 ASC->AddLooseGameplayTag(Tags.State_CombatMode);
 UProject_JGameplayAbility_Melee* Ability = nullptr;
 bool bReenter = false;
 int32 Reentries = 0;
 // ASC broadcasts copied delegates. This earlier subscriber deliberately ends
 // and starts the ability before the old input subscriber gets its turn.
 const FGameplayTagContainer AllEvents;
 const auto EarlierHandle = ASC->AddGameplayEventTagContainerDelegate(AllEvents,
  FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([&](FGameplayTag EventTag, const FGameplayEventData*)
  {
   if (!bReenter || EventTag != Tags.InputTag_Weapon_LMB) { return; }
   bReenter = false; ++Reentries;
   ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
   ASC->AbilityInputTagPressed(Tags.InputTag_Weapon_LMB);
   FGameplayEventData Initial; Initial.EventTag = Tags.InputTag_Weapon_LMB;
   ASC->HandleGameplayEvent(Tags.InputTag_Weapon_LMB, &Initial);
  }));
 ASC->AbilityInputTagPressed(Tags.InputTag_Weapon_LMB);
 for (const auto& Spec : ASC->GetActivatableAbilities())
 { if (Spec.IsActive()) { Ability = Cast<UProject_JGameplayAbility_Melee>(Spec.GetPrimaryInstance()); if (Ability) { break; } } }
 if (!TestNotNull(TEXT("Actual granted melee ability activated"), Ability))
 { ASC->RemoveGameplayEventTagContainerDelegate(AllEvents, EarlierHandle); Cleanup(); return false; }
 FGameplayEventData Payload; Payload.EventTag = Tags.State_Dead;
 ASC->HandleGameplayEvent(Tags.InputTag_Weapon_LMB, &Payload); // Starts the first node.
 TestTrue(TEXT("Activation owns one live input subscription"), Ability->ComboInputHandle.IsValid());
 TestTrue(TEXT("Input routing uses the subscribed authored set"), Ability->AcceptsComboInput(Tags.InputTag_Weapon_LMB));
 TestFalse(TEXT("Starting the first node does not queue a second attack"), Ability->bHasNextComboQueued);
 ASC->HandleGameplayEvent(Tags.State_Dead, &Payload);
 TestFalse(TEXT("Unrelated events cannot queue combo input"), Ability->bHasNextComboQueued);
 ASC->HandleGameplayEvent(Tags.InputTag_Weapon_LMB, &Payload);
 TestTrue(TEXT("Exact authored input still buffers while window is closed"), Ability->bHasNextComboQueued);
 TestTrue(TEXT("Dispatch tag replaces a mismatched payload tag"), Ability->QueuedInputTag == Tags.InputTag_Weapon_LMB);
 const uint64 OldBinding = Ability->ComboInputBindingRevision;
 bReenter = true;
 ASC->HandleGameplayEvent(Tags.InputTag_Weapon_LMB, &Payload);
 TestEqual(TEXT("Earlier callback reactivated once"), Reentries, 1);
 TestTrue(TEXT("Reactivation owns a different subscription identity"), Ability->ComboInputBindingRevision > OldBinding && Ability->ComboInputHandle.IsValid());
 TestTrue(TEXT("New attack remains active"), Ability->IsActive());
 TestFalse(TEXT("Copied old subscriber cannot queue input in the new activation"), Ability->bHasNextComboQueued);
 ASC->CancelAbilityHandle(Ability->GetCurrentAbilitySpecHandle());
 TestFalse(TEXT("Cancellation unbinds the original ASC"), Ability->ComboInputHandle.IsValid());
 TestTrue(TEXT("Cancellation retires input tags and owner"), Ability->ComboInputTags.IsEmpty() && !Ability->ComboInputASC.IsValid());
 ASC->HandleGameplayEvent(Tags.InputTag_Weapon_LMB, &Payload);
 TestFalse(TEXT("Late input after cancellation does not queue a combo"), Ability->bHasNextComboQueued);
 ASC->RemoveGameplayEventTagContainerDelegate(AllEvents, EarlierHandle);
 Cleanup(); return true;
}
#endif
