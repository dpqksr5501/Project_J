#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Mount/Project_JMountComponent.h"

// Reuse the existing authored-Avatar fixture; this requires the project's normal LFS assets.
namespace ProjectJRuntimeOwnershipTests
{
AProject_JSpellbladeCharacter* SpawnAvatar(UWorld*, AProject_JPlayerState*);
void Begin(UWorld*);
void End(UWorld*);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLifeTest, "ProjectJ.PlayerLife.DeathExplicitReviveAndRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLifeTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World)) return false;
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* PC = World->SpawnActor<APlayerController>();
    State->SetOwner(PC); PC->SetPlayerState(State);
    auto* Player = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Authored player fixture"), Player)) return false;
    PC->Possess(Player);
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    auto* Attr = State->GetProjectJAttributeSet();
    auto* Registry = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    if (!TestNotNull(TEXT("Registry"), Registry)) return false;
    TestTrue(TEXT("Live current player is target registered"), Registry->GetTargetCount() == 1);
    const float AttackBefore = Attr->GetAttackPower();
    const float ManaBefore = Attr->GetMana();
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0);
    TestFalse(TEXT("Death blocks life-gated input"), Player->IsPlayerLifeActive());
    TestEqual(TEXT("Death withdraws registry membership"), Registry->GetTargetCount(), 0);
    TestTrue(TEXT("Death tag published"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Dead));
    TestEqual(TEXT("Server movement policy stops translation"), Player->GetMoverMaxSpeed(false), 0.f);
    // Healing is not an implicit respawn or possession reset.
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), Attr->GetMaxHealth());
    TestFalse(TEXT("Ordinary healing does not revive a retired life"), Player->IsPlayerLifeActive());
    TestFalse(TEXT("Positive health cannot bypass death latch in melee"), Project_J::Combat::CanPlayerMeleeAttack(Player));
    TestFalse(TEXT("Invalid revive fraction denied"), Player->ReviveAuthority(0));
    TestTrue(TEXT("Explicit revive succeeds"), Player->ReviveAuthority(0.5f));
    TestTrue(TEXT("Explicit revive restores melee eligibility"), Project_J::Combat::CanPlayerMeleeAttack(Player));
    TestEqual(TEXT("Revive restores only requested health"), Attr->GetHealth(), Attr->GetMaxHealth() * 0.5f);
    TestEqual(TEXT("Attack preserved"), Attr->GetAttackPower(), AttackBefore);
    TestEqual(TEXT("Mana preserved"), Attr->GetMana(), ManaBefore);
    TestEqual(TEXT("Revive readmits registry membership"), Registry->GetTargetCount(), 1);
    TestFalse(TEXT("Repeated revive denied"), Player->ReviveAuthority());
    auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>();
    if (!TestNotNull(TEXT("Mount fixture"), Mount)) return false;
    Mount->SetActorLocation(Player->GetActorLocation());
    if (!TestTrue(TEXT("Public mounting succeeds"), Mount->TryMountRider(Player))) return false;
    TestTrue(TEXT("Mounted persistent ASC avatar remains rider"), ASC->GetAvatarActor() == Player);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0);
    TestFalse(TEXT("Lethal damage ends rider life while mounted"), Player->IsPlayerLifeActive());
    TestNull(TEXT("Lethal damage releases mount seat"), Mount->GetRider());
    TestFalse(TEXT("Dead rider cannot mount again"), Mount->TryMountRider(Player));
    TestTrue(TEXT("Explicit post-dismount revive"), Player->ReviveAuthority());
    ASC->ClearActorInfo();
    TestEqual(TEXT("Avatar retirement withdraws old target"), Registry->GetTargetCount(), 0);
    TestFalse(TEXT("Retired avatar cannot revive"), Player->ReviveAuthority());
    return true;
}
#endif
