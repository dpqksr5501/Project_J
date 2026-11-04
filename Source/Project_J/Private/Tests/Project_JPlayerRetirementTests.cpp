#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JPlayerRetirement.h"
#include "Tests/Project_JPlayerRetirementFixture.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Mount/Project_JMountComponent.h"
namespace ProjectJRuntimeOwnershipTests
{
AProject_JSpellbladeCharacter* SpawnAvatar(UWorld*, AProject_JPlayerState*);
void Begin(UWorld*);
void End(UWorld*);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerRetirementTest, "ProjectJ.Session.AvatarRetirementPreservesSharedMount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerRetirementTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World)) return false;
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* PC = World->SpawnActor<AProject_JPlayerRetirementFixture>();
    State->SetOwner(PC); PC->SetPlayerState(State);
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Authored avatar fixture"), Avatar)) return false;
    PC->Possess(Avatar);
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    TestFalse(TEXT("Revoked retirement fence performs no cleanup"), ProjectJ::Lifecycle::RetireConnectionAvatar(PC, [] { return false; }));
    TestTrue(TEXT("Invalid fence preserves possession"), PC->GetPawn() == Avatar);
    auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>();
    if (!TestNotNull(TEXT("Shared mount fixture"), Mount)) return false;
    Mount->SetActorLocation(Avatar->GetActorLocation());
    if (!TestTrue(TEXT("Mount via production entry point"), Mount->TryMountRider(Avatar))) return false;
    TestTrue(TEXT("Persistent avatar is rider"), ASC->GetAvatarActor() == Avatar);
    TestTrue(TEXT("Terminal retirement succeeds"), ProjectJ::Lifecycle::RetireConnectionAvatar(PC, [] { return true; }));
    TestNull(TEXT("Controller relinquishes mount"), PC->GetPawn());
    TestNull(TEXT("Shared mount relinquishes seat"), Mount->GetRider());
    TestFalse(TEXT("Shared mount is never destroyed"), Mount->IsActorBeingDestroyed());
    TestTrue(TEXT("Disconnected humanoid is destroyed"), Avatar->IsActorBeingDestroyed());
    TestNull(TEXT("Persistent ASC no longer publishes retired avatar"), ASC->GetAvatarActor());
    TestFalse(TEXT("Repeated terminal cleanup is harmless"), ProjectJ::Lifecycle::RetireConnectionAvatar(PC, [] { return true; }));
    // A settled callback can publish a newer avatar during ClearActorInfo. The old
    // retirement must stop before actor destruction, even with the same state.
    auto* Next = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Next authored avatar"), Next)) return false;
    PC->Possess(Next);
    auto* Replacement = World->SpawnActor<APawn>();
    bool bLeaseCurrent = true;
    const FDelegateHandle Reentry = ASC->OnActorInfoSettled.AddLambda([&](UProject_JAbilitySystemComponent* Source)
    {
        if (bLeaseCurrent && !Source->GetAvatarActor())
        {
            bLeaseCurrent = false;
            Source->InitAbilityActorInfo(State, Replacement);
            PC->Possess(Replacement);
        }
    });
    PC->LeaveForTest();
    ASC->OnActorInfoSettled.Remove(Reentry);
    TestTrue(TEXT("Replacement avatar retained"), ASC->GetAvatarActor() == Replacement);
    TestTrue(TEXT("Actual PawnLeavingGame preserves replacement possession"), PC->GetPawn() == Replacement);
    TestFalse(TEXT("Unproven old avatar is not destroyed by stale callback"), Next->IsActorBeingDestroyed());
    TestFalse(TEXT("Replacement actor is not destroyed"), Replacement->IsActorBeingDestroyed());
    return true;
}
#endif
