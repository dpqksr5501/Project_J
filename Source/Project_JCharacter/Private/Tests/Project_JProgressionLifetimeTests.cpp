#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Project_JAbilitySystemComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionLifetimeTest, "ProjectJ.RuntimeOwnership.ProgressionRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProgressionLifetimeTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    for (bool bShared : {false, true})
    {
        auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
        Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
        auto* Progression = NewObject<UProject_JProgressionComponent>(Owner); Progression->RegisterComponent();
        auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId = TEXT("LifetimeClass");
        auto* Set = NewObject<UProject_JAbilitySet>(); FProject_JAbilitySet_GameplayAbility Entry; Entry.Ability = UGameplayAbility::StaticClass(); Set->GrantedAbilityEntries.Add(Entry); Class->AbilitySets.Add(Set);
        Progression->InitializeClass(Class);
        FProject_JAbilitySet_GrantedHandles Other;
        if (bShared) { Set->GiveToAbilitySystem(ASC, &Other, Owner, FName(*(TEXT("Class.LifetimeClass.") + Set->GetPathName()))); }
        TestEqual(TEXT("Class grants one real GAS spec"), ASC->GetActivatableAbilities().Num(), 1);
        Progression->DestroyComponent();
        TestEqual(TEXT("Retirement returns its lease and preserves another provider"), ASC->GetActivatableAbilities().Num(), bShared ? 1 : 0);
        TestFalse(TEXT("Destroyed progression cannot grant late XP"), Progression->GrantExperience(100));
        TestFalse(TEXT("Destroyed progression cannot change level"), Progression->SetLevel(3));
        if (bShared) { Set->TakeFromAbilitySystem(ASC, &Other); }
        TestEqual(TEXT("Final provider release leaves no spec"), ASC->GetActivatableAbilities().Num(), 0);
    }
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    auto* Progression = NewObject<UProject_JProgressionComponent>(Owner); Progression->RegisterComponent();
    auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId = TEXT("RetiredDuringGrant");
    auto* Set = NewObject<UProject_JAbilitySet>(); FProject_JAbilitySet_GameplayEffect Effect;
    Effect.Effect = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/GEs/BP_GE_CombatMode.BP_GE_CombatMode_C"));
    Set->GrantedEffectEntries.Add(Effect); Class->AbilitySets.Add(Set);
    const auto Delegate = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Progression->DestroyComponent(); });
    TestFalse(TEXT("An initialization retired by its real GE callback reports failure"), Progression->InitializeClass(Class));
    ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
    TestEqual(TEXT("Unpublished class grant is returned after synchronous retirement"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
    return true;
}
#endif
