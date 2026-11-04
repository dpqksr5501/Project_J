#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "Backend/Project_JWorldEventClaimStorage.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
using namespace ProjectJ::MMO;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventPersistenceTest, "ProjectJ.MMO.Persistence.WorldEventClaimEpochRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldEventPersistenceTest::RunTest(const FString&)
{
    const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/WorldEventClaims") /
        FGuid::NewGuid().ToString(EGuidFormats::Digits) / TEXT("Profiles.sqlite"));
    // Real SQLite port must execute off the game thread. This focused native fixture has no network socket.
    const auto Errors = Async(EAsyncExecution::ThreadPool, [File]
    {
        namespace Claims = ProjectJ::WorldEventClaims;
        TArray<FString> Failures;
        auto Check = [&](bool OK, const TCHAR* Message) { if (!OK) Failures.Add(Message); return OK; };
        FProject_JSQLiteRepository DB(File);
        FProject_JLocalProfileSnapshot Values;
        Values.Account = FGuid::NewGuid(); Values.Character = FGuid::NewGuid();
        Values.Progression.Level = 1; Values.Progression.Experience = 25; Values.RulesFingerprint = FString::ChrN(40, 'A');
        TArray<uint8> Core; if (!Check(Values.Encode(Core), TEXT("Profile fixture encodes"))) return Failures;
        const FGuid Round = FGuid::NewGuid(), Session = FGuid::NewGuid();
        const FAggregateKey Lease{TEXT("LocalProfileLease"), Values.Character}, Profile{TEXT("LocalProfile"), Values.Character};
        auto Bytes = [](FGuid Id) { TArray<uint8> Result; FMemoryWriter Writer(Result); Writer << Id; return Result; };
        auto LeaseBytes = Bytes(Session);
        if (!Check(DB.CommitProfile({FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, 0, 1, LeaseBytes, false}}},
            {Values.Character, Session, -1}).Status == ECommitStatus::Committed, TEXT("Open initial epoch"))) return Failures;
        TArray<Claims::FClaim> Ledger{{Round, 25}}; TArray<uint8> ClaimBytes;
        if (!Check(Claims::Encode(Ledger, ClaimBytes), TEXT("Claim ledger encodes"))) return Failures;
        FCommitRequest First{FGuid(Session.A, Session.B, Session.C, 1), TEXT("ProfileEdit.WorldEventTest"),
            {{Lease, 1, 1, LeaseBytes, false}, {Profile, 0, 1, Core, false}, {Claims::Key(Values.Character), 0, 1, ClaimBytes, false}}};
        DB.bTestLoseCommitResponse = true;
        Check(DB.CommitProfile(First, {Values.Character, Session, 1}, {1}).Status == ECommitStatus::UnknownOutcome,
            TEXT("Lose acknowledgement after atomic XP+round commit"));
        Check(DB.CommitProfile(First, {Values.Character, Session, 1}, {1}).Status == ECommitStatus::Replayed,
            TEXT("Exact lost-ack retry does not grant twice"));
        DB.Close(); // reopen actual file, not an in-memory receipt cache
        FProject_JSQLiteRepository Reopened(File);
        const FGuid NewSession = FGuid::NewGuid(); LeaseBytes = Bytes(NewSession);
        Check(Reopened.CommitProfile({FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, 2, 1, LeaseBytes, false}}},
            {Values.Character, NewSession, -1}).Status == ECommitStatus::Committed, TEXT("Reconnect replaces epoch and retires receipts"));
        Check(Reopened.FindProfileReceipt(First.IdempotencyKey, First.Operation, {Values.Character, Session, 1}, {1}).Status == ECommitStatus::Expired,
            TEXT("Original operation receipt is expired after reconnect"));
        FRecord Stored, Marker; TArray<Claims::FClaim> ReadClaims; FProject_JLocalProfileSnapshot Restored;
        Check(Reopened.Read(Profile, Stored) && FProject_JLocalProfileSnapshot::Decode(Stored.Payload, Restored) &&
            Restored.Progression.Experience == 25 && Stored.Revision == 1, TEXT("Restart restores XP exactly once"));
        Check(Reopened.Read(Claims::Key(Values.Character), Marker) && Claims::Decode(Marker, ReadClaims) &&
            ReadClaims.Num() == 1 && ReadClaims[0].Round == Round && ReadClaims[0].Experience == 25,
            TEXT("Durable round proof survives receipt epoch cleanup"));
        // Even a bad caller cannot append the already paid round with a fresh epoch request.
        auto Duplicate = First; Duplicate.IdempotencyKey = FGuid(NewSession.A, NewSession.B, NewSession.C, 1);
        Duplicate.Mutations[0] = {Lease, 3, 1, LeaseBytes, false}; Duplicate.Mutations[1].ExpectedRevision = 1;
        Duplicate.Mutations[2].ExpectedRevision = 1;
        auto DoublePaid = Values; DoublePaid.Progression.Experience = 50;
        if (!Check(DoublePaid.Encode(Duplicate.Mutations[1].Payload), TEXT("Duplicate proposed XP encodes"))) return Failures;
        Check(Reopened.CommitProfile(Duplicate, {Values.Character, NewSession, 1}, {1}).Status == ECommitStatus::Invalid,
            TEXT("New request cannot rewrite old ledger and grant XP again"));
        auto Extra = Ledger; Extra.Add({Round, 25}); TArray<uint8> Bad;
        Check(!Claims::Encode(Extra, Bad), TEXT("Duplicate round in appended ledger rejected"));
        Extra = Ledger; Extra.Add({FGuid::NewGuid(), 10});
        Check(Claims::Encode(Extra, Bad) && Claims::IsAppend(Marker, Bad), TEXT("Distinct later round can append"));
        Extra[0].Experience = 26; Claims::Encode(Extra, Bad);
        Check(!Claims::IsAppend(Marker, Bad), TEXT("Prior payout terms cannot change"));
        Check(Reopened.Read(Profile, Stored) && Stored.Revision == 1 && FProject_JLocalProfileSnapshot::Decode(Stored.Payload, Restored) &&
            Restored.Progression.Experience == 25, TEXT("Rejected duplicate atomically leaves XP and revision unchanged"));
        TArray<Claims::FClaim> Full;
        for (int32 I = 0; I < Claims::MaxClaims; ++I) Full.Add({FGuid::NewGuid(), 1});
        Check(Claims::Encode(Full, Bad), TEXT("Bounded ledger capacity admitted"));
        Full.Add({FGuid::NewGuid(), 1});
        Check(!Claims::Encode(Full, Bad), TEXT("Capacity never evicts old once-only markers"));
        Reopened.Close(); return Failures;
    }).Get();
    for (const auto& Error : Errors) AddError(Error);
    return Errors.IsEmpty();
}
#endif
