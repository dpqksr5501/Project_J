#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Combat/Project_JGameplayEffect_CombatMode.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Equipment/Project_JGameplayEffect_EquipmentStats.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Misc/DataValidation.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAbilitySetEffectLevelTest,
    "ProjectJ.Continuation.AbilitySetEffectLevel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAbilitySetEffectLevelTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    auto* Set = NewObject<UProject_JAbilitySet>();
    FProject_JAbilitySet_GameplayEffect Entry; Entry.Effect = UProject_JGameplayEffect_CombatMode::StaticClass();
    Set->GrantedEffectEntries.Add(Entry);
    int32 Applied = 0; float AppliedLevel = 0;
    const auto Listener = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle) { ++Applied; AppliedLevel = Spec.GetLevel(); });
    ON_SCOPE_EXIT { ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Listener); };
    for (const float Level : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.5f})
    {
        Set->GrantedEffectEntries[0].EffectLevel = Level;
        FDataValidationContext Context;
        TestEqual(TEXT("Authoring requires finite EffectLevel at least 1"), Set->IsDataValid(Context), EDataValidationResult::Invalid);
        FProject_JAbilitySet_GrantedHandles Handles;
        Set->GiveToAbilitySystem(ASC, &Handles);
        TestEqual(TEXT("Malformed authored level is rejected before GAS publication"), Applied, 0);
        Set->TakeFromAbilitySystem(ASC, &Handles);
        Applied = 0;
    }
    Set->GrantedEffectEntries[0].EffectLevel = 1.5f;
    FProject_JAbilitySet_GrantedHandles Handles;
    Set->GiveToAbilitySystem(ASC, &Handles);
    TestEqual(TEXT("Valid fractional level above 1 is preserved"), AppliedLevel, 1.5f);
    TestEqual(TEXT("Valid entry applies once"), Applied, 1);
    Set->TakeFromAbilitySystem(ASC, &Handles);
    TestEqual(TEXT("Persistent grant releases normally"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentStatLeaseTest,
    "ProjectJ.Continuation.EquipmentStatLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentStatLeaseTest::RunTest(const FString&)
{
    for (const auto Policy : {EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly,
                             EProject_JEquipmentStatApplicationPolicy::GameplayEffectsThenStatModifiers})
    {
        for (int32 Mode = 0; Mode < 3; ++Mode)
        {
            auto* World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
            auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
            auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
            Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
            auto* Stats = NewObject<UProject_JAttributeSet>(Owner); ASC->AddAttributeSetSubobject(Stats);
            ASC->SetNumericAttributeBase(Stats->GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(Stats->GetHealthAttribute(), 90.f);
            ASC->SetNumericAttributeBase(Stats->GetAttackPowerAttribute(), 100.f);
            auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner); Manager->RegisterComponent();
            auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner); Runtime->RegisterComponent();
            Runtime->BindToEquipmentManager(Manager);
            auto* Item = NewObject<UProject_JEquipmentItemDefinition>();
            Item->StatApplicationPolicy = Policy;
            FProject_JEquipmentStatModifier Modifier;
            Modifier.Stat = Mode == 0 ? EProject_JEquipmentStat::MaxHealth : EProject_JEquipmentStat::AttackPower;
            Modifier.Value = Mode == 0 ? -150.f : 10.f;
            Item->StatModifiers.Add(Modifier);
            Manager->EquipItem(Item);
            if (Mode == 0)
            {
                TestEqual(TEXT("Negative modifier respects the current-value floor"), Stats->GetMaxHealth(), 0.f);
                TestEqual(TEXT("Equipment never overwrites permanent maximum base"), ASC->GetNumericAttributeBase(Stats->GetMaxHealthAttribute()), 100.f);
            }
            else
            {
                TestEqual(TEXT("Equipment bonus is present"), Stats->GetAttackPower(), 110.f);
                if (Mode == 1) { Item->StatModifiers[0].Value = 99.f; }
                else
                {
                    ASC->SetNumericAttributeBase(Stats->GetAttackPowerAttribute(), 120.f);
                    TestEqual(TEXT("Independent base progression retains the live equipment bonus"), Stats->GetAttackPower(), 130.f);
                }
            }
            Manager->UnequipItem(Item);
            TestEqual(TEXT("Removal uses the applied lease rather than a mutable definition or inverse clamp"),
                Mode == 0 ? Stats->GetMaxHealth() : Stats->GetAttackPower(), Mode == 2 ? 120.f : 100.f);
            TestEqual(TEXT("Maximum restoration does not heal a clamped resource"), Stats->GetHealth(), Mode == 0 ? 0.f : 90.f);
            TestEqual(TEXT("Unequip returns all equipment effect handles"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentStatTupleTest,
    "ProjectJ.Continuation.EquipmentStatTuple", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentStatTupleTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    auto* Stats = NewObject<UProject_JAttributeSet>(Owner); ASC->AddAttributeSetSubobject(Stats);
    ASC->SetNumericAttributeBase(Stats->GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(Stats->GetHealthAttribute(), 90.f);
    ASC->SetNumericAttributeBase(Stats->GetAttackPowerAttribute(), 100.f);
    auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
    for (int32 Invalid = 0; Invalid < 3; ++Invalid)
    {
        FProject_JEquipmentStatModifier Good; Good.Value = 10;
        auto Bad = Good;
        if (Invalid == 0) { Bad.Value = std::numeric_limits<float>::quiet_NaN(); }
        else if (Invalid == 1) { Bad.Stat = static_cast<EProject_JEquipmentStat>(255); }
        else { Good.Value = Bad.Value = MAX_flt; }
        Item->StatModifiers = {Good, Bad};
        FProject_JEquipmentStatValues Values; Values.Values[0] = 42.f;
        TestFalse(TEXT("Whole malformed tuple is rejected"), FProject_JEquipmentStatValues::TryResolve(Item->StatModifiers, Values));
        TestEqual(TEXT("Rejected resolution cannot partially publish values"), Values.Values[0], 42.f);
        FDataValidationContext Context;
        TestEqual(TEXT("Definition authoring shares numeric and enum admission"), Item->IsDataValid(Context), EDataValidationResult::Invalid);
        TestFalse(TEXT("Runtime rejects a malformed tuple before any modifier"),
            UProject_JGameplayEffect_EquipmentStats::ApplyBonuses(*ASC, Item->StatModifiers, Item).IsValid());
        TestEqual(TEXT("Good prefix of rejected tuple has no partial effect"), Stats->GetAttackPower(), 100.f);
    }
    FProject_JEquipmentStatModifier Negative; Negative.Stat = EProject_JEquipmentStat::MaxHealth; Negative.Value = -150;
    auto Positive = Negative; Positive.Value = 100;
    Item->StatModifiers = {Negative, Positive};
    const auto First = UProject_JGameplayEffect_EquipmentStats::ApplyBonuses(*ASC, Item->StatModifiers, Item);
    TestTrue(TEXT("Valid summed tuple owns a removable handle"), First.IsValid());
    TestEqual(TEXT("Per-stat totals precede clamping; order cannot change the bonus"), Stats->GetMaxHealth(), 50.f);
    TestEqual(TEXT("Maximum reduction clamps current resource"), Stats->GetHealth(), 50.f);
    Positive.Value = 25; Item->StatModifiers = {Positive};
    const auto Second = UProject_JGameplayEffect_EquipmentStats::ApplyBonuses(*ASC, Item->StatModifiers, Item);
    TestEqual(TEXT("Independent equipment leases combine"), Stats->GetMaxHealth(), 75.f);
    ASC->RemoveActiveGameplayEffect(First);
    TestEqual(TEXT("Removing one lease preserves the other's bonus and base"), Stats->GetMaxHealth(), 125.f);
    TestEqual(TEXT("Removing a penalty cannot heal"), Stats->GetHealth(), 50.f);
    ASC->RemoveActiveGameplayEffect(Second);
    TestEqual(TEXT("All leases returned preserves original permanent base"), Stats->GetMaxHealth(), 100.f);
    return true;
}
#endif
