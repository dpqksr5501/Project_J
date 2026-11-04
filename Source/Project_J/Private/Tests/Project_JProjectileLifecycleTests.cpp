#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JGameplayAbility_Projectile.h"
#include "Combat/Project_JProjectileAttackDefinition.h"
#include "Combat/Project_JCombatProjectile.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Project_JProjectileTestDamage.h"
#include "TimerManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProjectileLifecycleTest, "ProjectJ.Combat.ProjectileLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProjectileLifecycleTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* CDO = GetMutableDefault<UProject_JGameplayAbility_Projectile>();
 const FGameplayTag OldTag = CDO->AttackTag;
 ON_SCOPE_EXIT { CDO->AttackTag = OldTag; World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Player = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
 Player->SetPlayerState(State);
 auto* ASC = State->GetProjectJAbilitySystemComponent();
 ASC->InitAbilityActorInfo(State, Player); ASC->AddAttributeSetSubobject(State->GetProjectJAttributeSet());
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
 auto* Target = World->SpawnActor<AProject_JNPCCharacter>(Params);
 auto* TargetASC = Target->GetAbilitySystemComponent(); TargetASC->InitAbilityActorInfo(Target, Target);
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 Player->SetActorEnableCollision(false);
 Player->SetActorLocation(FVector(0, 0, 1000)); Target->SetActorLocation(FVector(200, 0, 1000));
 Target->SetActorEnableCollision(true);
 Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Target->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
 auto* Definition = NewObject<UProject_JProjectileAttackDefinition>(Player);
 Definition->AttackTag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
 Definition->DamageEffect = UProject_JProjectileTestDamage::StaticClass();
 CDO->AttackTag = Definition->AttackTag;
 auto* Style = NewObject<UProject_JCombatStyleDefinition>(Player);
 Style->CombatStyleTag = Definition->AttackTag; Style->bUsesCombo = false; Style->bRequiresWeaponAnimation = false;
 Style->bDeriveAttackCatalog = true; Style->AdditionalAttacks.Add(Definition);
 FProject_JAbilitySet_GameplayAbility Entry; Entry.Ability = UProject_JGameplayAbility_Projectile::StaticClass();
 Entry.InputTag = Definition->AttackTag; Entry.AbilityLevel = 1; Style->InlineAbilities.Add(Entry);
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Player);
 Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon; Item->CombatStyleDefinition = Style;
 auto* Equipment = State->GetEquipmentManagerComponent();
 Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->BindToEquipmentManager(Equipment);
 Equipment->EquipItem(Item);
 auto* Spec = ASC->FindAbilitySpecFromClass(UProject_JGameplayAbility_Projectile::StaticClass());
 if (!TestNotNull(TEXT("Equipment style grants projectile ability"), Spec)) return false;
 const auto Handle = Spec->Handle;
 auto* Ability = CastChecked<UProject_JGameplayAbility_Projectile>(Spec->GetPrimaryInstance());
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
 const auto Start = [&]() { return ASC->TryActivateAbility(Handle); };
 TestTrue(TEXT("Equipment catalog supplies projectile attack"), Start());
 TestTrue(TEXT("Windup retains active GAS execution"), Ability->IsActive());
 ASC->CancelAbilityHandle(Handle);
 World->GetTimerManager().Tick(1.f);
 TestFalse(TEXT("Prelaunch cancel invalidates execution"), Ability->IsActive());
 TestFalse(TEXT("Prelaunch cancel creates no projectile"), Ability->ActiveProjectile.IsValid());
 TestTrue(TEXT("Can start fresh after cancellation"), Start());
 Ability->Launch();
 auto* Shot = Ability->ActiveProjectile.Get();
 if (!TestNotNull(TEXT("Server launches authored projectile"), Shot)) return false;
 const uint64 Serial = Ability->Serial;
 Shot->Tick(.2f);
 TestTrue(TEXT("Authoritative swept contact consumes the projectile"), Shot->bConsumed);
 TestEqual(TEXT("Health reduced by authored effect"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);
 TestFalse(TEXT("Duplicate contact cannot submit second effect"), Ability->ConfirmProjectileHit(Shot, Serial, Target));
 TestEqual(TEXT("Duplicate damage is absent"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);
 TestFalse(TEXT("Real impact EndPlay ends GAS execution"), Ability->IsActive());
 TestTrue(TEXT("Next execution can start"), Start()); Ability->Launch();
 auto* Next = Ability->ActiveProjectile.Get();
 if (!TestNotNull(TEXT("Next projectile exists"), Next)) return false;
 TestFalse(TEXT("Old projectile cannot use new execution"), Ability->ConfirmProjectileHit(Shot, Serial, Target));
 Next->ExpiresAt = World->GetTimeSeconds() - 1.; Next->Tick(.016f);
 TestTrue(TEXT("Lifetime expiry consumes shot"), Next->bConsumed);
 TestFalse(TEXT("Real expiry EndPlay releases GAS execution"), Ability->IsActive());
 TestTrue(TEXT("Third execution starts"), Start()); Ability->Launch();
 auto* Cancelled = Ability->ActiveProjectile.Get();
 ASC->CancelAbilityHandle(Handle);
 TestTrue(TEXT("Cancellation after launch retires projectile"), Cancelled && Cancelled->bConsumed);
 TestEqual(TEXT("Cancelled flight does not damage"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);

 auto* Wall = World->SpawnActor<AActor>(Params);
 auto* Box = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(10, 100, 100)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
 Box->RegisterComponent(); Wall->SetActorLocation(FVector(100, 0, 1000));
 TestTrue(TEXT("Wall test execution starts"), Start()); Ability->Launch();
 auto* WallShot = Ability->ActiveProjectile.Get();
 if (!TestNotNull(TEXT("Wall shot exists"), WallShot)) return false;
 WallShot->Tick(.2f);
 TestTrue(TEXT("Wall consumes non-piercing shot"), WallShot->bConsumed);
 TestEqual(TEXT("Wall prevents damage behind it"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);
 TestFalse(TEXT("Wall contact ends ability"), Ability->IsActive());
 Wall->SetActorLocation(Player->GetActorLocation());
 TestTrue(TEXT("Initial-overlap execution starts"), Start()); Ability->Launch();
 auto* OverlapShot = Ability->ActiveProjectile.Get();
 if (!TestNotNull(TEXT("Overlap shot exists"), OverlapShot)) return false;
 OverlapShot->Tick(.2f);
 TestTrue(TEXT("Initial overlap cannot tunnel past wall"), OverlapShot->bConsumed);
 TestEqual(TEXT("Initial overlap gives no damage"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);
 Wall->Destroy();
 auto* Friend = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 auto* FriendState = World->SpawnActor<AProject_JPlayerState>(Params);
 Friend->SetPlayerState(FriendState);
 State->SetSocialState(TEXT("projectile-party"), NAME_None, FGuid(), FGuid());
 FriendState->SetSocialState(TEXT("projectile-party"), NAME_None, FGuid(), FGuid());
 Friend->SetActorLocation(FVector(100, 0, 1000));
 Friend->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Friend->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
 TestTrue(TEXT("Party blocker execution starts"), Start()); Ability->Launch();
 auto* PartyShot = Ability->ActiveProjectile.Get();
 if (!TestNotNull(TEXT("Party shot exists"), PartyShot)) return false;
 PartyShot->Tick(.2f);
 TestTrue(TEXT("Friendly contact consumes non-piercing shot"), PartyShot->bConsumed);
 TestEqual(TEXT("Party member blocks damage to NPC behind it"), TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 90.f);
 TestFalse(TEXT("Party block ends ability"), Ability->IsActive());
 return true;
}
#endif
