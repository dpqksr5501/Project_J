#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponAttackLifetimeTest, "ProjectJ.Combat.WeaponAttackLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponAttackLifetimeTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>(Params);
 auto* Target = World->SpawnActor<AProject_JNPCCharacter>(Params);
 Player->SetActorEnableCollision(false); Target->SetActorEnableCollision(false);
 Player->SetActorLocation(FVector(10000, 0, 1000)); Target->SetActorLocation(FVector(10100, 0, 1000));
 auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
 FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
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
 Hit->EndAttack(); Runtime->BindToEquipmentManager(nullptr);
 TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
 World->DestroyWorld(false);
 return true;
}
#endif
