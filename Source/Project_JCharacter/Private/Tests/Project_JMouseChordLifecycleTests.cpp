#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMouseChordLifecycleTest, "ProjectJ.Input.MouseChordLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMouseChordLifecycleTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
 auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
 FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
 ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
 auto* Router = Player->FindComponentByClass<UProject_JSkillInputRouterComponent>(); Router->Initialize(Player);
 const auto& Tags = FProject_JGameplayTags::Get();
 auto* Mapping = NewObject<UProject_JSkillInputMappingData>(Router);
 Mapping->Chords = {{}};
 Mapping->Chords[0].bRequiresLMB = true; Mapping->Chords[0].InputTag = Tags.InputTag_Weapon_LMB;
 auto& Right = Mapping->Chords.AddDefaulted_GetRef(); Right.bRequiresLMB = false; Right.bRequiresRMB = true; Right.InputTag = Tags.InputTag_Weapon_RMB;
 auto& Combined = Mapping->Chords.AddDefaulted_GetRef(); Combined.bRequiresLMB = Combined.bRequiresRMB = true;
 Combined.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Weapon.Chord.LMBRMB")); Combined.Priority = 100;
 Router->InputMappingData = Mapping;
 FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass(), 1);
 Spec.GetDynamicSpecSourceTags().AddTag(Tags.InputTag_Weapon_RMB);
 const auto Handle = ASC->GiveAbility(Spec); // No weapon: activation is rejected, but held/released GAS intent remains observable.
 int32 LeftEvents = 0, RightEvents = 0, ChordEvents = 0;
 const auto AdvanceGrace = [&] { ++GFrameCounter; World->GetTimerManager().Tick(.1f); };
 bool bReenter = false;
 ASC->GenericGameplayEventCallbacks.FindOrAdd(Tags.InputTag_Weapon_LMB).AddLambda([&](const FGameplayEventData*) { ++LeftEvents; });
 ASC->GenericGameplayEventCallbacks.FindOrAdd(Tags.InputTag_Weapon_RMB).AddLambda([&](const FGameplayEventData*)
 {
  ++RightEvents;
  if (bReenter) { bReenter = false; Router->HandleButtonPressed(EProject_JSkillInputButton::RMB); }
 });
 ASC->GenericGameplayEventCallbacks.FindOrAdd(Combined.InputTag).AddLambda([&](const FGameplayEventData*) { ++ChordEvents; });
 Router->HandleButtonPressed(EProject_JSkillInputButton::LMB); Router->HandleButtonReleased(EProject_JSkillInputButton::LMB);
 Router->HandleButtonPressed(EProject_JSkillInputButton::RMB); Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
 TestEqual(TEXT("Quick left tap delivered once"), LeftEvents, 1);
 TestEqual(TEXT("Quick right tap delivered once"), RightEvents, 1);
 TestFalse(TEXT("Quick tap releases actual GAS spec intent"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
 Router->HandleButtonPressed(EProject_JSkillInputButton::LMB); Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
 Router->HandleButtonReleased(EProject_JSkillInputButton::LMB); Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
 TestEqual(TEXT("Simultaneous chord preserves authored priority"), ChordEvents, 1);
 TestEqual(TEXT("Chord did not manufacture a single right tap"), RightEvents, 1);
 Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
 AdvanceGrace();
 Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
 TestEqual(TEXT("Held single tap dispatches once after existing grace"), RightEvents, 2);
 Router->HandleButtonPressed(EProject_JSkillInputButton::RMB); Router->CancelMouseInput();
 AdvanceGrace();
 TestEqual(TEXT("Focus cancellation cannot replay a pending press"), RightEvents, 2);
 bReenter = true;
 Router->HandleButtonPressed(EProject_JSkillInputButton::RMB); Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
 AdvanceGrace();
 TestTrue(TEXT("Old quick-tap completion cannot release a reentrant new press"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
 Router->HandleButtonReleased(EProject_JSkillInputButton::RMB);
 TestFalse(TEXT("New completion releases its own GAS intent"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
 ASC->GenericGameplayEventCallbacks.Empty();
 World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
