#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/BoxComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "GameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponAttackLifetimeTest, "ProjectJ.Combat.WeaponAttackLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponAttackLifetimeTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Player = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 auto* Target = World->SpawnActor<AProject_JNPCCharacter>(Params);
 Player->SetActorEnableCollision(false); Target->SetActorEnableCollision(false);
 Player->SetActorLocation(FVector(10000, 0, 1000)); Target->SetActorLocation(FVector(10100, 0, 1000));
 auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
 FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
 auto* Attributes = NewObject<UProject_JAttributeSet>(Player); ASC->AddAttributeSetSubobject(Attributes);
 FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AttributeSet"))->SetObjectPropertyValue_InContainer(Player, Attributes);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 auto* TargetASC = Target->GetAbilitySystemComponent(); TargetASC->InitAbilityActorInfo(Target, Target);
 auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Player); Equipment->RegisterComponent();
 auto* Runtime = Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>(); Runtime->BindToEquipmentManager(Equipment);
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Player); Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
 auto* Hit = Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
 auto* Attack = NewObject<UProject_JAttackDefinition>(Player); Attack->AttackTag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
 Attack->DamageEffect = UGameplayEffect::StaticClass();
 int32 Applied = 0;
 const auto Delegate = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
  [&Applied](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { ++Applied; });
 const FVector Start = Player->GetActorLocation(), End = Target->GetActorLocation();
 const auto Open = [&](int32 Key) { Hit->BeginAttackNode(Attack->AttackTag, Attack, Key); Hit->SetHitWindowOpen(true); Hit->RecordAuthoritativeTrace(Start, End); };
 Equipment->EquipItem(Item); const uint64 Revision = Runtime->GetWeaponRevision();
 Open(11);
 TestTrue(TEXT("Equipped authority hit applies a real effect"), Hit->ProcessAuthorityHit(Target));
 Open(11); // Fresh window, so dedup cannot hide a missing equipment guard.
 Equipment->UnequipSlot(EProject_JEquipmentSlot::Weapon);
 TestFalse(TEXT("Revoked weapon rejects authority hit even without an ability end callback"), Hit->ProcessAuthorityHit(Target));
 Hit->ServerRequestSSRHit_Implementation(Target, 0, Start, End, Attack->AttackTag, 1, 11);
 TestEqual(TEXT("Late network request after unequip applies nothing"), Applied, 1);
 Equipment->EquipItem(Item);
 TestTrue(TEXT("Same asset re-equipped receives a new identity"), Runtime->GetWeaponRevision() != Revision);
 TestFalse(TEXT("Re-equip does not revive old hit window"), Hit->ProcessAuthorityHit(Target));
 Open(22);
 FProject_JCombatHitRequest Request; Request.Target = Target; Request.AttackNodeTag = Attack->AttackTag;
 Request.TraceStart = Start; Request.TraceEnd = End; Request.RequestSequence = 2; Request.PredictionKey = 11;
 TestTrue(TEXT("Old activation rejected during a new same-tag swing"), Hit->ValidateActiveAttack(Request) == EProject_JCombatHitValidationFailure::AttackActivationMismatch);
 Hit->ServerRequestSSRHit_Implementation(Target, 0, Start, End, Attack->AttackTag, 3, 11);
 TestEqual(TEXT("Old activation cannot damage after re-equip"), Applied, 1);
 Request.PredictionKey = 22; Request.RequestSequence = 4;
 TestTrue(TEXT("Current predicted activation passes active-window validation"), Hit->ValidateActiveAttack(Request) == EProject_JCombatHitValidationFailure::None);
 TestTrue(TEXT("New authority swing still damages"), Hit->ProcessAuthorityHit(Target));
 TestEqual(TEXT("Only pre-revoke and new attacks applied effects"), Applied, 2);
 const int32 OriginalRequestLimit = Hit->MaxRequestsPerSecond;
 Hit->MaxRequestsPerSecond = 2; // Per-instance admission contract, not a production tuning value.
 const auto CheckSSRAdmission = [&]
 {
  auto* Wall = World->SpawnActor<AActor>(); auto* Blocker = NewObject<UBoxComponent>(Wall);
  Wall->SetRootComponent(Blocker); Wall->AddInstanceComponent(Blocker); Blocker->SetBoxExtent(FVector(5,50,100));
  Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Blocker->SetCollisionObjectType(ECC_WorldStatic);
  Blocker->SetCollisionResponseToAllChannels(ECR_Ignore); Blocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
  Blocker->RegisterComponent(); Wall->SetActorLocation((Start+End)*.5);
  FHitResult Obstruction;
  if (!TestTrue(TEXT("Actual visibility blocker is present before admission ordering checks"),
   World->LineTraceSingleByChannel(Obstruction, Start, End, ECC_Visibility) && Obstruction.GetActor() == Wall && Hit->IsTargetObstructed(Target))) return;
  const int32 BeforeDuplicateCount = Hit->RequestsInRateWindow, BeforeDuplicateSequence = Hit->LastServerRequestSequence;
  Request.RequestSequence = BeforeDuplicateSequence + 1;
  TestEqual(TEXT("An already confirmed target rejects before the actual world obstruction"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::DuplicateTarget);
  TestTrue(TEXT("Duplicate rejection consumes no new domain count or sequence"), Hit->RequestsInRateWindow == BeforeDuplicateCount && Hit->LastServerRequestSequence == BeforeDuplicateSequence);
  Open(33); Request.PredictionKey = 33; Request.RequestSequence = BeforeDuplicateSequence;
  TestEqual(TEXT("Old sequence rejects before the actual world obstruction"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::RequestRateLimited);
  TestTrue(TEXT("Replay rejection consumes no new domain count or sequence"), Hit->RequestsInRateWindow == BeforeDuplicateCount && Hit->LastServerRequestSequence == BeforeDuplicateSequence);
  const auto AdvanceDomainWindow = [&]
  {
   const double Until = Hit->RateWindowStartSeconds + 1.01;
   for (int32 Step = 0; World->GetTimeSeconds() < Until && Step < 32; ++Step) World->Tick(LEVELTICK_All, .1f);
   return World->GetTimeSeconds() >= Until;
  };
  if (!TestTrue(TEXT("Normal small world ticks reach the next domain rate window"), AdvanceDomainWindow())) return;
  Request.ClientTimestamp = World->GetTimeSeconds(); Request.RequestSequence = BeforeDuplicateSequence + 1;
  TestEqual(TEXT("First eligible blocked attempt still reports the real obstruction"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::WorldObstructed);
  TestTrue(TEXT("A blocked eligible request owns one domain slot and its sequence"), Hit->RequestsInRateWindow == 1 && Hit->LastServerRequestSequence == Request.RequestSequence);
  TestEqual(TEXT("The same blocked sequence cannot repeat an unbudgeted LOS"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::RequestRateLimited);
  TestEqual(TEXT("Blocked replay keeps the first domain charge"), Hit->RequestsInRateWindow, 1);
  ++Request.RequestSequence;
  TestEqual(TEXT("A second eligible blocked attempt consumes the remaining slot"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::WorldObstructed);
  TestTrue(TEXT("Second blocked admission commits its sequence and count"), Hit->RequestsInRateWindow == 2 && Hit->LastServerRequestSequence == Request.RequestSequence);
  const int32 LastBlockedSequence = Hit->LastServerRequestSequence;
  ++Request.RequestSequence;
  TestEqual(TEXT("Domain saturation rejects before the actual world obstruction"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::RequestRateLimited);
  TestTrue(TEXT("Over-limit attempt retains existing count semantics without committing its sequence"), Hit->RequestsInRateWindow == 3 && Hit->LastServerRequestSequence == LastBlockedSequence);
  Hit->EndAttack(); Hit->ResetSpatialHistory(); Open(44); Request.PredictionKey = 44;
  TestTrue(TEXT("Attack end spatial reset and a new window preserve the domain replay fence"), Hit->RequestsInRateWindow == 3 && Hit->LastServerRequestSequence == LastBlockedSequence);
  Request.RequestSequence = LastBlockedSequence;
  TestEqual(TEXT("A new attack window cannot reuse the old sequence"), Hit->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::RequestRateLimited);
  const int32 BeforeInvalidCount = Hit->RequestsInRateWindow, BeforeInvalidSequence = Hit->LastServerRequestSequence;
  Hit->ServerRequestSSRHit_Implementation(Player, World->GetTimeSeconds(), Start, End, Attack->AttackTag, LastBlockedSequence+10, 44);
  TestTrue(TEXT("Invalid actor RPC leaves domain admission and damage untouched"), Hit->RequestsInRateWindow == BeforeInvalidCount && Hit->LastServerRequestSequence == BeforeInvalidSequence && Applied == 2);
  auto* Budget = NewObject<UProject_JCommandBudgetComponent>(Player);
  Player->AddInstanceComponent(Budget); Budget->RegisterComponent(); Budget->CommandsPerSecond = 1;
  const int64 BeforeOuterRejection = Budget->GetRejectedCount();
  Hit->ServerRequestSSRHit_Implementation(Target, std::numeric_limits<double>::quiet_NaN(), Start, End, Attack->AttackTag, LastBlockedSequence+11, 44);
  TestTrue(TEXT("Malformed basic RPC leaves domain count sequence and damage untouched"), Hit->RequestsInRateWindow == BeforeInvalidCount && Hit->LastServerRequestSequence == BeforeInvalidSequence && Applied == 2);
  TestFalse(TEXT("Rejected malformed RPC still consumes its actual outer command admission"), Budget->TryConsume());
  TestEqual(TEXT("Outer shared budget records the subsequent explicit rejection"), Budget->GetRejectedCount(), BeforeOuterRejection+1);
  Budget->DestroyComponent(); // Return the fixture's temporary outer budget through its public lifecycle.
  Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision); Wall->Destroy();
  if (!TestTrue(TEXT("Native world time restores the original domain window naturally"), AdvanceDomainWindow())) return;
  Open(55); Target->SetActorEnableCollision(true);
  TestFalse(TEXT("Normal recovery has no remaining world obstruction"), Hit->IsTargetObstructed(Target));
  Hit->ServerRequestSSRHit_Implementation(Target, World->GetTimeSeconds(), Start, End, Attack->AttackTag, LastBlockedSequence+1, 55);
  TestTrue(TEXT("A fresh admissible request commits normally after natural recovery"), Hit->RequestsInRateWindow == 1 && Hit->LastServerRequestSequence == LastBlockedSequence+1);
  TestEqual(TEXT("Recovered network request applies a real effect through current sweep history"), Applied, 3);
 };
 CheckSSRAdmission();
 Hit->MaxRequestsPerSecond = OriginalRequestLimit;
 Hit->EndAttack(); Runtime->BindToEquipmentManager(nullptr);
 TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
 World->DestroyWorld(false);
 GEngine->DestroyWorldContext(World);
 return true;
}
#endif
