#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Project_JAbilitySystemComponent.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGrantReentryTest, "ProjectJ.RuntimeOwnership.GrantSourceReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGrantReentryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Owner = World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
    Owner->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
    auto* EffectClass = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/GEs/BP_GE_CombatMode.BP_GE_CombatMode_C"));
    if (!TestNotNull(TEXT("Actual persistent effect class"), EffectClass)) { return false; }
    auto* First = NewObject<UProject_JAbilitySet>();
    FProject_JAbilitySet_GameplayEffect Entry; Entry.Effect = EffectClass; First->GrantedEffectEntries.Add(Entry);
    auto* Replacement = NewObject<UProject_JAbilitySet>();
    FProject_JAbilitySet_GrantedHandles Handles;
    constexpr auto Source = TEXT("Test.ReentrantSharedSource");
    bool bReplaced = false;
    const auto Delegate = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) {
            if (bReplaced) { return; }
            bReplaced = true;
            First->TakeFromAbilitySystem(ASC, &Handles);
            Replacement->GiveToAbilitySystem(ASC, &Handles, Owner, Source);
        });
    First->GiveToAbilitySystem(ASC, &Handles, Owner, Source);
    ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
    TestTrue(TEXT("Actual apply callback replaces the same source identity"), bReplaced);
    TestTrue(TEXT("Replacement owns its own source reservation"), ASC->HasAbilityGrantSource(Source));
    TestEqual(TEXT("Older apply cannot append an effect to new handles"), Handles.GameplayEffectHandles.Num(), 0);
    TestEqual(TEXT("The retired generation's persistent effect is returned"), ASC->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
    Replacement->TakeFromAbilitySystem(ASC, &Handles);
    TestFalse(TEXT("Replacement reservation releases normally"), ASC->HasAbilityGrantSource(Source));
    return true;
}
#endif
