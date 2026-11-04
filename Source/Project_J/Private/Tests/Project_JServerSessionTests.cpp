#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Player.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "Social/Project_JSocialSubsystem.h"
#include "Testing/Project_JNetworkLoadFixture.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJServerSessionNativeOwnershipTest, "ProjectJ.MMO.ServerSession.NativeOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJServerSessionNativeOwnershipTest::RunTest(const FString&)
{
    using namespace ProjectJ::MMO;
    const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    ON_SCOPE_EXIT
    {
        World->EndPlay(EEndPlayReason::LevelTransition);
        Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    FURL URL; URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
    if (!TestTrue(TEXT("Use native authority GameMode installation"), World->SetGameMode(URL))) return false;
    World->InitializeActorsForPlay(URL);
    World->GetWorldSettings()->NotifyBeginPlay();
    auto* Mode = Cast<AProject_JNetworkLoadGameMode>(World->GetAuthGameMode());
    if (!TestNotNull(TEXT("Existing concrete fixture inherits the trusted native entry"), Mode)) return false;
    auto* FirstPC = World->SpawnActor<APlayerController>();
    auto* SecondPC = World->SpawnActor<APlayerController>();
    // This is a Standalone native ownership test, not a socket/remote proof.
    FirstPC->SetAsLocalPlayerController(); SecondPC->SetAsLocalPlayerController();
    if (!FirstPC->GetPlayerState<AProject_JPlayerState>()) FirstPC->InitPlayerState();
    if (!SecondPC->GetPlayerState<AProject_JPlayerState>()) SecondPC->InitPlayerState();
    auto* First = FirstPC->GetPlayerState<AProject_JPlayerState>();
    auto* Second = SecondPC->GetPlayerState<AProject_JPlayerState>();
    if (!TestNotNull(TEXT("Native player state one"), First) || !TestNotNull(TEXT("Native player state two"), Second)) return false;
    TestTrue(TEXT("Unbound prototype commands retain the existing path"), UProject_JCommandBudgetComponent::AdmitClientCommand(First));
    // Concrete UPlayer supplies native PostLogin's transport metadata without
    // creating a viewport or claiming this local test is a network connection.
    FirstPC->SetPlayer(NewObject<UPlayer>(Instance.Get()));
    SecondPC->SetPlayer(NewObject<UPlayer>(Instance.Get()));
    Mode->bRequireTrustedSessions = true;
    Mode->PostLogin(FirstPC); Mode->PostLogin(SecondPC);
    TestTrue(TEXT("Trusted-mode native PostLogin holds both Pawns and prototype identities"),
        !FirstPC->GetPawn() && !SecondPC->GetPawn() && !First->GetAccountId().IsValid() && !Second->GetCharacterId().IsValid());
    TestFalse(TEXT("Native starting-player entry is held before verification"), First->CanAcceptSessionCommand());
    const auto Account = FProject_JAccountId::NewId();
    const auto Character = FProject_JCharacterId::NewId();
    FServerSessionAttempt Attempt; FServerSessionLease FirstLease, SecondLease, Rotated;
    TestTrue(TEXT("Server begins after principal verification"), Mode->BeginTrustedSession(FirstPC, Account, Character, Attempt));
    TestFalse(TEXT("An explicitly pending state cannot dispatch gameplay commands"), UProject_JCommandBudgetComponent::AdmitClientCommand(First));
    TestFalse(TEXT("Failed ownership verification cannot install a session"), Mode->CompleteTrustedSession(Attempt, false, FirstLease));
    TestFalse(TEXT("Cancelled admission never reopens the legacy command bypass"), First->CanAcceptSessionCommand());
    TestTrue(TEXT("Trusted server starts a new verification attempt"), Mode->BeginTrustedSession(FirstPC, Account, Character, Attempt));
    if (!TestTrue(TEXT("Verified ownership installs current lease"), Mode->CompleteTrustedSession(Attempt, true, FirstLease))) return false;
    TestTrue(TEXT("Exact account published"), First->GetAccountId().Value == Account.Value);
    TestTrue(TEXT("Exact persistent character published"), First->GetCharacterId().Value == Character.Value);
    const auto CapturedBeforeReconnect = First->CaptureServerSession();
    TestTrue(TEXT("Actual shared command consumer admits the accepted lease"), UProject_JCommandBudgetComponent::AdmitClientCommand(First));
    auto* OldPawn = World->SpawnActor<APawn>(); FirstPC->Possess(OldPawn);
    auto* OldASC = First->GetProjectJAbilitySystemComponent(); OldASC->InitAbilityActorInfo(First, OldPawn);
    const auto OldGrant = OldASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    if (!TestTrue(TEXT("A real old-avatar ability execution is active"), OldASC->TryActivateAbility(OldGrant))) return false;
    int32 OldExecutionEndCount = 0;
    bool bOldExecutionCancelledBeforeDestruction = false;
    const auto OldEndHandle = OldASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
    {
        if (Data.AbilitySpecHandle != OldGrant) return;
        ++OldExecutionEndCount;
        bOldExecutionCancelledBeforeDestruction = Data.bWasCancelled
            && !FirstPC->IsActorBeingDestroyed() && !First->IsActorBeingDestroyed() && !OldASC->IsBeingDestroyed();
    });
    ON_SCOPE_EXIT { OldASC->OnAbilityEnded.Remove(OldEndHandle); };
    TestTrue(TEXT("A reconnect uses the same persistent IDs on another native connection"), Mode->BeginTrustedSession(SecondPC, Account, Character, Attempt));
    if (!TestTrue(TEXT("New verified connection takes the single writer"), Mode->CompleteTrustedSession(Attempt, true, SecondLease))) return false;
    TestTrue(TEXT("Native Mover/input ownership is withdrawn through UnPossess"), FirstPC->GetPawn() == nullptr && OldPawn->GetController() == nullptr);
    TestTrue(TEXT("Old avatar actor info is cleared"), OldASC->GetAvatarActor() == nullptr);
    OldASC->OnAbilityEnded.Remove(OldEndHandle);
    TestEqual(TEXT("Terminal retirement ends the captured execution exactly once"), OldExecutionEndCount, 1);
    TestTrue(TEXT("Execution is explicitly cancelled before controller/state/ASC destruction"), bOldExecutionCancelledBeforeDestruction);
    TestTrue(TEXT("Different-connection replacement terminates the old controller"), FirstPC->IsActorBeingDestroyed());
    // Terminal controller teardown can also retire the PlayerState-owned ASC and
    // remove its grants. Retention belongs to same-connection renewal, below.
    const auto* RetiredGrant = OldASC->FindAbilitySpecFromHandle(OldGrant);
    TestTrue(TEXT("No active old grant survives terminal retirement"), !RetiredGrant || !RetiredGrant->IsActive());
    TestFalse(TEXT("Old captured work cannot apply after reconnect"), First->IsServerSessionCurrent(CapturedBeforeReconnect));
    TestFalse(TEXT("Revoked PlayerState cannot dispatch another command"), UProject_JCommandBudgetComponent::AdmitClientCommand(First));
    TestFalse(TEXT("Delayed logout for old epoch cannot detach the replacement"), Mode->RevokeTrustedSession(First, FirstLease));
    TestTrue(TEXT("New connection remains current"), Second->IsServerSessionCurrent(SecondLease));
    const auto UnexpectedCharacter = FProject_JCharacterId::NewId();
    Second->SetIdentity(Account, UnexpectedCharacter);
    TestTrue(TEXT("Managed identity cannot bypass the trusted character lease"), Second->GetCharacterId().Value == Character.Value);

    auto* SecondPawn = World->SpawnActor<APawn>(); SecondPC->Possess(SecondPawn);
    auto* NewPawn = World->SpawnActor<APawn>();
    auto* ASC = Second->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(Second, SecondPawn);
    const auto Grant = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    if (!TestTrue(TEXT("Second avatar has an actual cancellable execution"), ASC->TryActivateAbility(Grant))) return false;
    bool Reentered = false;
    const auto EndHandle = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
    {
        if (Data.AbilitySpecHandle == Grant && !Reentered)
        { Reentered = true; ASC->InitAbilityActorInfo(Second, NewPawn); }
    });
    ON_SCOPE_EXIT { ASC->OnAbilityEnded.Remove(EndHandle); };
    auto* Social = Instance->GetSubsystem<UProject_JSocialSubsystem>();
    if (!TestNotNull(TEXT("Social authority exists in native session fixture"), Social)) return false;
    const auto Party = Social->CreateParty(Second);
    if (!TestTrue(TEXT("Current session creates real party"), Party.bSucceeded)) return false;
    FProject_JPartyAuthorityFence BeforeEpochFence;
    if (!TestTrue(TEXT("Party authority captures current same-state epoch"), Social->CapturePartyAuthority(Second, BeforeEpochFence))) return false;
    const uint64 BeforeRotation = ASC->GetActorInfoRevision();
    TestTrue(TEXT("Same connection can begin a new session epoch"), Mode->BeginTrustedSession(SecondPC, Account, Character, Attempt));
    if (!TestTrue(TEXT("Same-state rotation completes"), Mode->CompleteTrustedSession(Attempt, true, Rotated))) return false;
    TestTrue(TEXT("Cancellation actually reentered actor-info publication"), Reentered && ASC->GetActorInfoRevision() > BeforeRotation);
    TestTrue(TEXT("Outer old cleanup cannot clear the newer avatar tuple"), ASC->GetAvatarActor() == NewPawn);
    const auto* RenewedGrant = ASC->FindAbilitySpecFromHandle(Grant);
    if (!TestTrue(TEXT("Same-connection renewal retains the grant but ends its old execution"), RenewedGrant && !RenewedGrant->IsActive())) return false;
    TestFalse(TEXT("Same-connection renewal preserves the controller"), SecondPC->IsActorBeingDestroyed());
    TestTrue(TEXT("Old native pawn is detached before new publication"), SecondPC->GetPawn() == nullptr && SecondPawn->GetController() == nullptr);
    TestFalse(TEXT("Captured old same-state epoch is stale"), Second->IsServerSessionCurrent(SecondLease));
    TestTrue(TEXT("Fresh epoch still accepts commands"), UProject_JCommandBudgetComponent::AdmitClientCommand(Second));
    TestFalse(TEXT("Party execution rejects former epoch on same PlayerState"), Social->IsPartyAuthorityCurrent(BeforeEpochFence));
    TestFalse(TEXT("Actual party mutation rejects same-state stale epoch"), Social->TransferPartyLeadership(BeforeEpochFence, Character.Value).bSucceeded);
    FProject_JPartyAuthorityFence AfterEpochFence;
    TestTrue(TEXT("Fresh same-state epoch captures party authority"), Social->CapturePartyAuthority(Second, AfterEpochFence));
    TestTrue(TEXT("Fresh fence retains unchanged party authority"), Social->IsPartyAuthorityCurrent(AfterEpochFence));
    ASC->OnAbilityEnded.Remove(EndHandle);
    SecondPC->Possess(NewPawn);
    ASC->CallServerTryActivateAbility(Grant, true, FPredictionKey());
    if (!TestTrue(TEXT("Accepted lease passes the actual native server activation entry"), ASC->FindAbilitySpecFromHandle(Grant)->IsActive())) return false;
    auto* RebasedPawn = World->SpawnActor<APawn>();
    bool RebasedDuringRevoke = false;
    const auto RevokeHandle = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
    {
        if (Data.AbilitySpecHandle == Grant && !RebasedDuringRevoke)
        { RebasedDuringRevoke = true; ASC->InitAbilityActorInfo(Second, RebasedPawn); }
    });
    ON_SCOPE_EXIT { ASC->OnAbilityEnded.Remove(RevokeHandle); };
    TestTrue(TEXT("Exact lease logout retires the current native owner"), Mode->RevokeTrustedSession(Second, Rotated));
    TestTrue(TEXT("Logout unpossesses only its current pawn"), SecondPC->GetPawn() == nullptr && NewPawn->GetController() == nullptr);
    TestTrue(TEXT("Revoke preserves a newer callback-published actor-info tuple"), RebasedDuringRevoke && ASC->GetAvatarActor() == RebasedPawn);
    TestFalse(TEXT("Revoked commands remain denied"), Second->CanAcceptSessionCommand());
    ASC->CallServerTryActivateAbility(Grant, true, FPredictionKey());
    TestFalse(TEXT("Valid actor info cannot bypass revoked session through native server activation"), ASC->FindAbilitySpecFromHandle(Grant)->IsActive());
    FServerAbilityRPCBatch Batch; Batch.AbilitySpecHandle = Grant; Batch.Started = true;
    ASC->ServerAbilityRPCBatch(Batch);
    TestFalse(TEXT("Whole native RPC batch also rejects revoked session"), ASC->FindAbilitySpecFromHandle(Grant)->IsActive());
    ASC->OnAbilityEnded.Remove(RevokeHandle);
    FServerSessionAttempt PendingAtEnd;
    TestTrue(TEXT("A final verification can be pending at world teardown"), Mode->BeginTrustedSession(SecondPC, Account, Character, PendingAtEnd));
    Mode->EndPlay(EEndPlayReason::LevelTransition);
    FServerSessionLease Late;
    TestFalse(TEXT("Late completion cannot resurrect a closed world"), Mode->CompleteTrustedSession(PendingAtEnd, true, Late));
    return true;
}
#endif
