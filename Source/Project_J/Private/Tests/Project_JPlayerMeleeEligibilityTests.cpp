#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerMeleeEligibilityTest, "ProjectJ.Combat.PlayerMeleeEligibility",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerMeleeEligibilityTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Player = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 auto* Friend = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(Params);
 auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
 auto* FriendState = World->SpawnActor<AProject_JPlayerState>(Params);
 Player->SetPlayerState(State); Friend->SetPlayerState(FriendState);
 auto* ASC = State->GetProjectJAbilitySystemComponent();
 ASC->InitAbilityActorInfo(State, Player); ASC->AddAttributeSetSubobject(State->GetProjectJAttributeSet());
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 const auto SetParty = [](AProject_JPlayerState* PS, FName Party) { PS->SetSocialState(Party, NAME_None, FGuid(), FGuid()); };
 for (auto* Actor : TArray<AActor*>{ Player, Friend, NPC }) Actor->SetActorEnableCollision(false);
 Player->SetActorLocation(FVector(10000, 0, 1000));
 Friend->SetActorLocation(FVector(10100, 0, 1000)); NPC->SetActorLocation(FVector(10100, 0, 1000));
 auto* NPCASC = NPC->GetAbilitySystemComponent(); NPCASC->InitAbilityActorInfo(NPC, NPC);
 auto* Equipment = State->GetEquipmentManagerComponent();
 Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->BindToEquipmentManager(Equipment);
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Player); Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
 Equipment->EquipItem(Item);
 auto* Hit = Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
 auto* Attack = NewObject<UProject_JAttackDefinition>(Player); Attack->AttackTag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
 Attack->DamageEffect = UGameplayEffect::StaticClass();
 const FVector Start = Player->GetActorLocation(), End = NPC->GetActorLocation();
 const auto Open = [&](int32 Key) { Hit->BeginAttackNode(Attack->AttackTag, Attack, Key); Hit->SetHitWindowOpen(true); Hit->RecordAuthoritativeTrace(Start, End); };
 using namespace Project_J::Combat;
 TestTrue(TEXT("Healthy bound player can start melee"), CanPlayerMeleeAttack(Player));
 TestEqual(TEXT("Two absent parties are not friends"), ValidatePlayerMeleeTarget(Player, Friend), EProject_JCombatHitValidationFailure::None);
 SetParty(State, TEXT("party-a")); SetParty(FriendState, TEXT("party-a"));
 TestEqual(TEXT("Same non-empty party rejects"), ValidatePlayerMeleeTarget(Player, Friend), EProject_JCombatHitValidationFailure::FriendlyPlayerTarget);
 TestEqual(TEXT("NPC does not inherit player party"), ValidatePlayerMeleeTarget(Player, NPC), EProject_JCombatHitValidationFailure::None);
 Open(101);
 TestFalse(TEXT("Party member cannot consume the only cleave target slot"), Hit->ProcessAuthorityHit(Friend, 1));
 int32 Applied = 0;
 const auto Handle = NPCASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
  [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { ++Applied; ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0.f); });
 TestTrue(TEXT("Enemy still receives the admitted hit"), Hit->ProcessAuthorityHit(NPC, 1));
 TestEqual(TEXT("Only the enemy effect was applied"), Applied, 1);
 TestFalse(TEXT("Lethal callback blocks new activation"), CanPlayerMeleeAttack(Player));
 Open(102); // Deliberately retain/open an execution: eligibility must not rely on a death callback cancelling it.
 TestFalse(TEXT("Dead attacker cannot apply another authority effect"), Hit->ProcessAuthorityHit(NPC));
 Hit->ServerRequestSSRHit_Implementation(NPC, World->GetTimeSeconds(), Start, End, Attack->AttackTag, 1, 102);
 TestEqual(TEXT("Late predicted request cannot add damage"), Applied, 1);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 SetParty(FriendState, TEXT("party-b"));
 TestEqual(TEXT("Different party does not receive a PvP restriction invented by this change"), ValidatePlayerMeleeTarget(Player, Friend), EProject_JCombatHitValidationFailure::None);
 ASC->InitAbilityActorInfo(State, Friend);
 TestFalse(TEXT("Old avatar cannot use the replacement avatar's health"), CanPlayerMeleeAttack(Player));
 NPCASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
 Hit->EndAttack();
 return true;
}
#endif
