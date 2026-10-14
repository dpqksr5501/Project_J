#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentCommitLifetimeTest,
    "ProjectJ.RuntimeOwnership.EquipmentCommitLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentCommitLifetimeTest::RunTest(const FString&)
{
    // Destruction, a null binding gap and a populated replacement are distinct boundaries.
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
        Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
        auto* Attributes = NewObject<UProject_JAttributeSet>(Owner); ASC->AddAttributeSetSubobject(Attributes);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetAttackPowerAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetDefenseAttribute(), 50.f);
        auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner); Manager->RegisterComponent();
        auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner); Runtime->RegisterComponent();
        Runtime->BindToEquipmentManager(Manager);
        auto* Definition = NewObject<UProject_JEquipmentItemDefinition>();
        Definition->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
        Definition->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
        FProject_JEquipmentStatModifier First; First.Stat = EProject_JEquipmentStat::AttackPower; First.Value = 10;
        FProject_JEquipmentStatModifier Second; Second.Stat = EProject_JEquipmentStat::Defense; Second.Value = 20;
        Definition->StatModifiers = {First, Second};
        auto* ReplacementManager = NewObject<UProject_JEquipmentManagerComponent>(Owner); ReplacementManager->RegisterComponent();
        auto* ReplacementDefinition = NewObject<UProject_JEquipmentItemDefinition>();
        ReplacementDefinition->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
        FProject_JEquipmentStatModifier Replacement; Replacement.Stat = EProject_JEquipmentStat::AttackPower; Replacement.Value = 30;
        ReplacementDefinition->StatModifiers = {Replacement}; ReplacementManager->EquipItem(ReplacementDefinition);
        uint64 ReplacementRevision = 0;
        bool bRetiredDuringApply = false;
        const auto Handle = ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetAttackPowerAttribute())
            .AddLambda([&](const FOnAttributeChangeData& Change)
            {
                if (!bRetiredDuringApply && Change.NewValue > 100)
                {
                    bRetiredDuringApply = true;
                    if (Mode == 0) { Runtime->DestroyComponent(); }
                    else if (Mode == 1) { Runtime->BindToEquipmentManager(nullptr); }
                    else { Runtime->BindToEquipmentManager(ReplacementManager); ReplacementRevision = Runtime->GetWeaponRevision(); }
                }
            });
        Manager->EquipItem(Definition);
        ASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetAttackPowerAttribute()).Remove(Handle);
        TestTrue(TEXT("Actual attribute callback retired the runtime during apply"), bRetiredDuringApply);
        TestEqual(TEXT("A retired runtime returns its unpublished attack modifier"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetAttackPowerAttribute()), (Mode == 2 ? 130.f : 100.f));
        TestEqual(TEXT("Rollback includes later modifiers and preserves the previous defense"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetDefenseAttribute()), 50.f);
        TestEqual(TEXT("Older apply cannot replace a subsequent binding's weapon lease"), Runtime->GetWeaponRevision(), ReplacementRevision);
        if (Mode == 2) { TestTrue(TEXT("New binding publishes its own live weapon"), ReplacementRevision != 0); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDestroyedCombatBindingTest,
    "ProjectJ.RuntimeOwnership.DestroyedCombatBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDestroyedCombatBindingTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    auto* Combat = NewObject<UProject_JCombatStateComponent>(Owner); Combat->RegisterComponent();
    Combat->BindToAbilitySystem(ASC);
    const auto Tag = FGameplayTag::RequestGameplayTag(TEXT("State.CombatMode"));
    ASC->AddProjectJLooseGameplayTag(Tag);
    TestTrue(TEXT("Live component observes the actual ASC"), Combat->IsCombatModeActive());
    Combat->DestroyComponent();
    TestFalse(TEXT("Destroyed component cannot read or command the live ASC"), Combat->IsCombatModeActive());
    Combat->BindToAbilitySystem(ASC);
    TestFalse(TEXT("Late binding cannot revive a destroyed combat component"), Combat->IsCombatModeActive());
    TestEqual(TEXT("Retiring a listener preserves the gameplay owner's tag"), ASC->GetTagCount(Tag), 1);
    return true;
}
#endif
