#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JPlayerState.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPartyAuthorityTest, "ProjectJ.MMO.Social.PartyAuthorityFence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPartyAuthorityTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JSocialSubsystem> Social(NewObject<UProject_JSocialSubsystem>(GI.Get()));
    auto* A = World->SpawnActor<AProject_JPlayerState>();
    auto* B = World->SpawnActor<AProject_JPlayerState>();
    auto* C = World->SpawnActor<AProject_JPlayerState>();
    for (auto* Player : {A, B, C})
    {
        Player->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
        Social->BindPlayerState(Player);
    }
    const auto Party = Social->CreateParty(A);
    TestTrue(TEXT("Create real party"), Party.bSucceeded);
    TestTrue(TEXT("Join B"), Social->JoinParty(B, Party.GroupId).bSucceeded);
    FProject_JPartyAuthorityFence OldA, CurrentA, CurrentB;
    TestTrue(TEXT("Capture leader intent"), Social->CapturePartyAuthority(A, OldA));
    TestFalse(TEXT("Member cannot capture leader authority"), Social->CapturePartyAuthority(B, CurrentB));
    TestTrue(TEXT("Roster changes invalidate previously captured intent"), Social->JoinParty(C, Party.GroupId).bSucceeded);
    TestFalse(TEXT("Old roster denied at mutation"), Social->TransferPartyLeadership(OldA, B->GetCharacterId().Value).bSucceeded);
    Social->CapturePartyAuthority(A, CurrentA);
    TestTrue(TEXT("Current leader transfers"), Social->TransferPartyLeadership(CurrentA, B->GetCharacterId().Value).bSucceeded);
    TestFalse(TEXT("Former leader late removal denied"), Social->RemovePartyMember(CurrentA, C->GetCharacterId().Value).bSucceeded);
    TestTrue(TEXT("New leader captures current authority"), Social->CapturePartyAuthority(B, CurrentB));
    TestTrue(TEXT("Current leader expels previous leader"), Social->RemovePartyMember(CurrentB, A->GetCharacterId().Value).bSucceeded);
    TestFalse(TEXT("Expelled character cannot regain old fence"), Social->IsPartyAuthorityCurrent(CurrentA));
    TestFalse(TEXT("One roster mutation invalidates replay"), Social->RemovePartyMember(CurrentB, C->GetCharacterId().Value).bSucceeded);
    Social->CapturePartyAuthority(B, CurrentB);
    Social->UnbindPlayerState(B);
    TestFalse(TEXT("Disconnect revokes captured authority"), Social->IsPartyAuthorityCurrent(CurrentB));
    auto* Replacement = World->SpawnActor<AProject_JPlayerState>();
    Replacement->SetIdentity(B->GetAccountId(), B->GetCharacterId()); Social->BindPlayerState(Replacement);
    TestFalse(TEXT("Same character new connection cannot replay old actor fence"), Social->IsPartyAuthorityCurrent(CurrentB));
    TestTrue(TEXT("Replacement may capture fresh current authority"), Social->CapturePartyAuthority(Replacement, CurrentB));
    return true;
}
#endif
