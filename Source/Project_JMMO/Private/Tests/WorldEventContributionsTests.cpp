#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MMO/WorldEventContributions.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldEventContributionsTest, "ProjectJ.MMO.WorldEventContributions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWorldEventContributionsTest::RunTest(const FString&)
{
    using namespace ProjectJ::MMO;
    FWorldEventContributions Ledger(2, 2);
    const FGuid Round = FGuid::NewGuid(), Next = FGuid::NewGuid(), A = FGuid::NewGuid(), B = FGuid::NewGuid();
    TestFalse(TEXT("Invalid round cannot start"), Ledger.Begin(FGuid()));
    TestFalse(TEXT("Unopened round cannot receive damage"), Ledger.Record(Round, A, 1));
    TestFalse(TEXT("Unopened round cannot resolve"), Ledger.Close(Round, true));
    TestTrue(TEXT("Round opens"), Ledger.Begin(Round));
    TestFalse(TEXT("No character identity cannot contribute"), Ledger.Record(Round, FGuid(), 1));
    TestFalse(TEXT("Zero damage does not join event"), Ledger.Record(Round, B, 0));
    TestFalse(TEXT("Healing does not join event"), Ledger.Record(Round, B, -1));
    TestFalse(TEXT("NaN is rejected"), Ledger.Record(Round, B, std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("Infinity is rejected"), Ledger.Record(Round, B, std::numeric_limits<double>::infinity()));
    TestEqual(TEXT("Rejected input creates no contribution"), Ledger.GetDamage(Round, B), 0.0);
    TestFalse(TEXT("Same round cannot reset contributions"), Ledger.Begin(Round));
    TestTrue(TEXT("Initial damage"), Ledger.Record(Round, A, 7));
    TestTrue(TEXT("Mid-round join"), Ledger.Record(Round, B, 2));
    TestTrue(TEXT("Reconnect uses same character, not connection"), Ledger.Record(Round, A, 3));
    TestEqual(TEXT("Contribution survives reconnect"), Ledger.GetDamage(Round, A), 10.0);
    TestFalse(TEXT("Alive encounter grants no entitlement"), Ledger.IsEligible(Round, A));
    TestTrue(TEXT("Defeat freezes round"), Ledger.Close(Round, true));
    TestFalse(TEXT("Repeated defeat cannot resolve twice"), Ledger.Close(Round, true));
    TestFalse(TEXT("Late damage rejected"), Ledger.Record(Round, A, 10));
    TestTrue(TEXT("Disconnected character remains eligible"), Ledger.IsEligible(Round, A));
    TestTrue(TEXT("Another round opens"), Ledger.Begin(Next));
    TestTrue(TEXT("Old entitlement retained"), Ledger.IsEligible(Round, A));
    Ledger.Record(Next, A, 1);
    TestEqual(TEXT("New round does not inherit old damage"), Ledger.GetDamage(Next, A), 1.0);
    TestEqual(TEXT("Old round retains exact damage"), Ledger.GetDamage(Round, A), 10.0);
    Ledger.Close(Next, false);
    TestFalse(TEXT("Cancellation is not defeat"), Ledger.IsEligible(Next, A));
    TestFalse(TEXT("Capacity never silently evicts pending rounds"), Ledger.Begin(FGuid::NewGuid()));
    FWorldEventContributions Bounded(1, 1); Bounded.Begin(Round); Bounded.Record(Round, A, 1);
    TestFalse(TEXT("Participant overflow reported"), Bounded.Record(Round, B, 1)); Bounded.Close(Round, true);
    TestFalse(TEXT("Incomplete round fails closed"), Bounded.IsEligible(Round, A));
    FWorldEventContributions Interrupted; Interrupted.Begin(Round); Interrupted.Record(Round, A, 1);
    Interrupted.Invalidate(Round); Interrupted.Close(Round, true);
    TestFalse(TEXT("Reentrant destruction during health observation cannot certify partial participation"), Interrupted.IsEligible(Round, A));
    FWorldEventContributions Overflow; Overflow.Begin(Round);
    TestTrue(TEXT("Finite large contribution accepted"), Overflow.Record(Round, A, std::numeric_limits<double>::max()));
    TestFalse(TEXT("Accumulation overflow reported"), Overflow.Record(Round, A, std::numeric_limits<double>::max()));
    Overflow.Close(Round, true);
    TestFalse(TEXT("Overflow cannot authorize incomplete payout"), Overflow.IsEligible(Round, A));
    Ledger.Invalidate(Round);
    TestFalse(TEXT("Late discovered incomplete proof revokes eligibility"), Ledger.IsEligible(Round, A));
    return true;
}
#endif
