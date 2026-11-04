#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Game/Project_JGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJServerClockPrecisionTest, "ProjectJ.Foundation.Network.ServerClockPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJServerClockPrecisionTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* State = World->SpawnActor<AProject_JGameState>(); World->SetGameState(State);
	const AGameStateBase* Query = State;
	const double ClientNow = World->GetTimeSeconds();
	TestEqual(TEXT("A new GameState has no synchronization samples"), State->NumServerWorldTimeSecondsDeltas, 0u);
	TestEqual(TEXT("Uninitialized authority clock keeps the original world-time boundary"), Query->GetServerWorldTimeSeconds(), ClientNow);
	State->SwapRoles();
	TestEqual(TEXT("Synchronization fixture models a simulated client state"), State->GetLocalRole(), ROLE_SimulatedProxy);
	const auto Sample = [&](double Offset)
	{
		State->ReplicatedWorldTimeSecondsDouble = ClientNow + Offset;
		State->OnRep_ReplicatedWorldTimeSecondsDouble(); // The actual engine writer owns sum/count.
	};
	constexpr double LongOffset = 1'048'576.0;
	Sample(LongOffset + .010);
	TestEqual(TEXT("Late join retains a sub-frame absolute offset"), Query->GetServerWorldTimeSeconds(), ClientNow + LongOffset + .010);
	Sample(LongOffset + .030);
	TestNearlyEqual(TEXT("The existing half-step filter preserves sub-frame offset precision"), Query->GetServerWorldTimeSeconds(), ClientNow + LongOffset + .015, .000001);
	TestEqual(TEXT("Queries never add their own synchronization samples"), State->NumServerWorldTimeSecondsDeltas, 2u);
	const double FilteredTime = Query->GetServerWorldTimeSeconds();
	TestEqual(TEXT("Reading the clock does not advance its filter again"), Query->GetServerWorldTimeSeconds(), FilteredTime);

	// A new world/replica starts with the original zero-count boundary. A real
	// zero-offset sample must still count as initialized, rather than a sentinel.
	auto* Fresh = World->SpawnActor<AProject_JGameState>();
	TestEqual(TEXT("A fresh replica does not inherit the previous world's samples"), Fresh->NumServerWorldTimeSecondsDeltas, 0u);
	TestEqual(TEXT("A fresh replica returns its original world time"), Fresh->GetServerWorldTimeSeconds(), ClientNow);
	Fresh->ReplicatedWorldTimeSecondsDouble = ClientNow;
	Fresh->OnRep_ReplicatedWorldTimeSecondsDouble();
	TestEqual(TEXT("An actual zero-offset sample initializes the clock"), Fresh->NumServerWorldTimeSecondsDeltas, 1u);
	TestEqual(TEXT("A valid zero offset preserves the correct clock"), Fresh->GetServerWorldTimeSeconds(), ClientNow);
	Fresh->ReplicatedWorldTimeSecondsDouble = ClientNow + .040;
	Fresh->OnRep_ReplicatedWorldTimeSecondsDouble();
	TestNearlyEqual(TEXT("The original zero sentinel installs the next mean without a half step"), Fresh->GetServerWorldTimeSeconds(), ClientNow + .020, .000000001);
	Fresh->ReplicatedWorldTimeSecondsDouble = ClientNow + .080;
	Fresh->OnRep_ReplicatedWorldTimeSecondsDouble();
	TestNearlyEqual(TEXT("A nonzero offset follows the original half-step filter"), Fresh->GetServerWorldTimeSeconds(), ClientNow + .030, .000000001);

	// Exercise the real 250/251 boundary rather than implementing a second mean
	// accumulator in the test or production query.
	auto* Rebased = World->SpawnActor<AProject_JGameState>();
	constexpr double Offset = LongOffset + .0123456;
	for (int32 Index = 0; Index < 250; ++Index)
	{
		Rebased->ReplicatedWorldTimeSecondsDouble = ClientNow + Offset;
		Rebased->OnRep_ReplicatedWorldTimeSecondsDouble();
	}
	TestEqual(TEXT("250 real synchronization samples retain the engine window"), Rebased->NumServerWorldTimeSecondsDeltas, 250u);
	TestNearlyEqual(TEXT("250-sample averaging retains sub-millisecond offset precision"), Rebased->GetServerWorldTimeSeconds(), ClientNow + Offset, .00001);
	const double PriorSum = Rebased->SumServerWorldTimeSecondsDelta;
	const double PriorOffset = Rebased->GetServerWorldTimeSeconds() - ClientNow;
	const double NextOffset = Offset + .020;
	Rebased->ReplicatedWorldTimeSecondsDouble = ClientNow + NextOffset;
	Rebased->OnRep_ReplicatedWorldTimeSecondsDouble();
	const double ExpectedRebaseMean = (PriorSum + NextOffset) / 251.0;
	const double ExpectedRebaseOffset = PriorOffset + (ExpectedRebaseMean - PriorOffset) * 0.5;
	TestEqual(TEXT("251st sample uses the engine's one-sample rebase"), Rebased->NumServerWorldTimeSecondsDeltas, 1u);
	TestEqual(TEXT("A rebase keeps the same half-step response without float narrowing"), Rebased->GetServerWorldTimeSeconds(), ClientNow + ExpectedRebaseOffset);
	const double FollowingOffset = Offset + .040;
	Rebased->ReplicatedWorldTimeSecondsDouble = ClientNow + FollowingOffset;
	Rebased->OnRep_ReplicatedWorldTimeSecondsDouble();
	const double ExpectedFollowingMean = (ExpectedRebaseMean + FollowingOffset) / 2.0;
	const double ExpectedFollowingOffset = ExpectedRebaseOffset + (ExpectedFollowingMean - ExpectedRebaseOffset) * 0.5;
	TestEqual(TEXT("The next sample continues the existing engine accumulator"), Rebased->NumServerWorldTimeSecondsDeltas, 2u);
	TestEqual(TEXT("Post-rebase offset follows the original filter response"), Rebased->GetServerWorldTimeSeconds(), ClientNow + ExpectedFollowingOffset);
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return !HasAnyErrors();
}
#endif
