#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MMO/FeatureCatalog.h"
#include "MMO/Repository.h"
#include "Async/ParallelFor.h"
#include <atomic>

using namespace ProjectJ::MMO;
namespace
{
FAggregateKey Key(const TCHAR* Kind) { return {FName(Kind), FGuid::NewGuid()}; }
FCommitRequest Write(const FAggregateKey& Target, int64 Revision, uint8 Value)
{
    return { FGuid::NewGuid(), TEXT("Test.Write"), {{Target, Revision, 1, {Value}, false}} };
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatalogTest, "ProjectJ.MMO.Catalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCatalogTest::RunTest(const FString&)
{
    auto Catalog = MakeFoundationCatalog(); FString Error; TArray<FName> Plan;
    TestTrue(TEXT("Entire extension catalog is acyclic and closed"), Catalog.Validate(Error));
    TestTrue(TEXT("Broad catalog exists"), Catalog.Num() >= 200);
    TestTrue(TEXT("Market dependency plan resolves"), Catalog.Resolve({TEXT("Economy.Market")}, Plan, Error));
    TestTrue(TEXT("Escrow before market"), Plan.IndexOfByKey(FName("Economy.Escrow")) < Plan.IndexOfByKey(FName("Economy.Market")));
    TestTrue(TEXT("Wallet inventory dependency is explicit"), Catalog.Resolve({TEXT("Economy.Vendor")}, Plan, Error) && Plan.Contains(FName("Items.Inventory")) && Plan.Contains(FName("Economy.Wallet")));
    TestFalse(TEXT("Unknown content fails closed"), Catalog.Resolve({TEXT("Economy.Market"), TEXT("Missing")}, Plan, Error));
    TestTrue(TEXT("Failed resolution clears partial plan"), Plan.IsEmpty());
    FFeatureCatalog Cyclic;
    TestTrue(TEXT("Forward references allowed during construction"), Cyclic.Register({TEXT("A"), TEXT("A"), TEXT("Test"), TEXT("Service"), {TEXT("B")}}, Error));
    TestFalse(TEXT("Duplicate registration rejected"), Cyclic.Register({TEXT("A"), TEXT("A"), TEXT("Test"), TEXT("Service"), {}}, Error));
    TestFalse(TEXT("Missing dependency rejected"), Cyclic.Validate(Error));
    Cyclic.Register({TEXT("B"), TEXT("B"), TEXT("Test"), TEXT("Service"), {TEXT("A")}}, Error);
    TestFalse(TEXT("Cycle rejected"), Cyclic.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositoryAtomicTest, "ProjectJ.MMO.Repository.AtomicReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRepositoryAtomicTest::RunTest(const FString&)
{
    FMemoryRepository Store; const auto Wallet = Key(TEXT("Wallet")), Inventory = Key(TEXT("Inventory"));
    auto Initial = Write(Wallet, 0, 100); TestEqual(TEXT("Seed"), Store.Commit(Initial).Status, ECommitStatus::Committed);
    FCommitRequest Purchase {FGuid::NewGuid(), TEXT("Purchase"), {{Wallet, 1, 1, {90}, false}, {Inventory, 2, 1, {1}, false}}};
    TestEqual(TEXT("Second record conflict rejects entire transaction"), Store.Commit(Purchase).Status, ECommitStatus::Conflict);
    FRecord Record; Store.Read(Wallet, Record); TestEqual(TEXT("Wallet unchanged after failed purchase"), Record.Payload[0], uint8(100));
    TestFalse(TEXT("Inventory was not created"), Store.Read(Inventory, Record));
    Purchase.Mutations[1].ExpectedRevision = 0;
    TestEqual(TEXT("Atomic purchase"), Store.Commit(Purchase).Status, ECommitStatus::Committed);
    const auto Replay = Store.Commit(Purchase);
    TestEqual(TEXT("Lost response replay"), Replay.Status, ECommitStatus::Replayed);
    TestEqual(TEXT("Original receipt returned"), Replay.Revisions[0], int64(2));
    Store.Read(Wallet, Record); TestEqual(TEXT("No double debit"), Record.Revision, int64(2));
    Purchase.Mutations[0].Payload[0] = 80;
    TestEqual(TEXT("Reusing key with different payload rejected"), Store.Commit(Purchase).Status, ECommitStatus::KeyReused);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositoryBoundsTest, "ProjectJ.MMO.Repository.BoundsAndTombstones",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRepositoryBoundsTest::RunTest(const FString&)
{
    FMemoryRepository Store(1, 3); const auto Item = Key(TEXT("Item")); auto Create = Write(Item, 0, 1);
    TestEqual(TEXT("Create"), Store.Commit(Create).Status, ECommitStatus::Committed);
    TestEqual(TEXT("Record capacity"), Store.Commit(Write(Key(TEXT("Item")), 0, 1)).Status, ECommitStatus::Capacity);
    auto Delete = Write(Item, 1, 0); Delete.Mutations[0].bDelete = true; Delete.Mutations[0].Payload.Reset();
    TestEqual(TEXT("Delete"), Store.Commit(Delete).Status, ECommitStatus::Committed);
    TestEqual(TEXT("Old create cannot resurrect deleted record"), Store.Commit(Write(Item, 0, 2)).Status, ECommitStatus::Conflict);
    FRecord Record; TestTrue(TEXT("Tombstone retains version"), Store.Read(Item, Record) && Record.bDeleted && Record.Revision == 2);
    TestEqual(TEXT("Authorized recreate at current version"), Store.Commit(Write(Item, 2, 3)).Status, ECommitStatus::Committed);
    TestEqual(TEXT("Receipts bounded without eviction"), Store.Commit(Write(Item, 3, 4)).Status, ECommitStatus::Capacity);
    TestEqual(TEXT("Replay survives full capacity"), Store.Commit(Create).Status, ECommitStatus::Replayed);
    auto Invalid = Write(Item, MAX_int64, 1);
    TestEqual(TEXT("Overflow rejected"), Store.Commit(Invalid).Status, ECommitStatus::Invalid);
    Invalid = Write(Item, 3, 1); const FMutation Duplicate = Invalid.Mutations[0]; Invalid.Mutations.Add(Duplicate);
    TestEqual(TEXT("Duplicate keys rejected"), Store.Commit(Invalid).Status, ECommitStatus::Invalid);
    Invalid = Write(Item, 3, 1); Invalid.Mutations[0].Payload.SetNum(65537);
    TestEqual(TEXT("Oversized payload rejected"), Store.Commit(Invalid).Status, ECommitStatus::Invalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepositoryConcurrencyTest, "ProjectJ.MMO.Repository.ConcurrentWriters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRepositoryConcurrencyTest::RunTest(const FString&)
{
    FMemoryRepository Store; const auto Target = Key(TEXT("Wallet")); std::atomic<int32> Winners {0};
    ParallelFor(128, [&](int32 I) { Winners += Store.Commit(Write(Target, 0, uint8(I))).Status == ECommitStatus::Committed; });
    TestEqual(TEXT("128 competing creates have exactly one winner"), Winners.load(), 1);
    FRecord Record; Store.Read(Target, Record); TestEqual(TEXT("One committed revision"), Record.Revision, int64(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGateTest, "ProjectJ.MMO.Coordination.MultiOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGateTest::RunTest(const FString&)
{
    FAggregateGate Gate(4); const auto A = Key(TEXT("Character")), B = Key(TEXT("Character")), C = Key(TEXT("Guild"));
    FGuid First, Second, Rejected;
    TestEqual(TEXT("Trade reserves both owners"), Gate.TryAcquire({A, B}, First), EAdmission::Accepted);
    TestEqual(TEXT("Conflicting operation rejected"), Gate.TryAcquire({C, B}, Rejected), EAdmission::Busy);
    TestFalse(TEXT("Rejected ticket invalid"), Rejected.IsValid());
    TestEqual(TEXT("Rejected multi-owner acquisition did not reserve C"), Gate.TryAcquire({C}, Second), EAdmission::Accepted);
    TestTrue(TEXT("Release trade"), Gate.Release(First));
    TestFalse(TEXT("Duplicate release"), Gate.Release(First));
    TestEqual(TEXT("Next owner operation"), Gate.TryAcquire({A}, Rejected), EAdmission::Accepted);
    TestFalse(TEXT("Old ticket cannot unlock replacement"), Gate.Release(First));
    Gate.Close();
    TestEqual(TEXT("No new work after close"), Gate.TryAcquire({B}, First), EAdmission::Closed);
    TestTrue(TEXT("In-flight work may finish after admission closes"), Gate.Release(Rejected));
    FAggregateGate Contended;
    std::atomic<int32> Active {0}, Violations {0}, Accepted {0};
    ParallelFor(512, [&](int32)
    {
        FGuid Ticket;
        if (Contended.TryAcquire({A, B}, Ticket) == EAdmission::Accepted)
        {
            ++Accepted;
            if (Active.fetch_add(1) != 0) { ++Violations; }
            Active.fetch_sub(1);
            Contended.Release(Ticket);
        }
    });
    TestTrue(TEXT("Contended owners make progress"), Accepted.load() > 0);
    TestEqual(TEXT("Concurrent multi-owner reservations never overlap"), Violations.load(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrackerTest, "ProjectJ.MMO.Coordination.RequestLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTrackerTest::RunTest(const FString&)
{
    FRequestTracker Tracker(2); FGuid A, B, C;
    TestEqual(TEXT("First"), Tracker.Begin(A), EAdmission::Accepted);
    TestEqual(TEXT("Second"), Tracker.Begin(B), EAdmission::Accepted);
    TestEqual(TEXT("Admission bounded"), Tracker.Begin(C), EAdmission::Capacity);
    std::atomic<int32> Completions {0};
    ParallelFor(128, [&](int32) { Completions += Tracker.Complete(A); });
    TestEqual(TEXT("Concurrent duplicate completions delivered once"), Completions.load(), 1);
    TestEqual(TEXT("Slot reusable"), Tracker.Begin(C), EAdmission::Accepted);
    Tracker.Close();
    TestFalse(TEXT("Late old-world response suppressed"), Tracker.Complete(B));
    TestEqual(TEXT("Closed session never reopens"), Tracker.Begin(A), EAdmission::Closed);
    TestEqual(TEXT("No retained pending requests"), Tracker.Num(), 0);
    return true;
}
#endif
