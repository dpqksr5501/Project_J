#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
	class FVisualAssetCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVisualAssetCommand(FAutomationTestBase* InTest, bool bCrowd = false) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Owner = World->SpawnActor<AActor>(); Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
			const FSoftObjectPath Path(TEXT("/Engine/BasicShapes/Cube.Cube"));
			const int32 Count = bCrowd ? 1024 : 25;
			Expected = bCrowd ? 960 : 24;
			for (int32 I = 0; I < Count; ++I)
			{
				if (bCrowd && I % 4 == 0) { Owner = World->SpawnActor<AActor>(); }
				Tokens.Add(Service->Request(Owner, Path, [this](UObject* Asset)
				{
					Test->TestNotNull(TEXT("Async engine fixture resolves"), Asset); ++Delivered;
				}));
			}
			Test->TestEqual(TEXT("Identical assets share one group"), Service->GetGroupCount(), 1);
			Service->Release(Tokens[0]);
			if (bCrowd) { for (int32 I = 16; I < Count; I += 16) { Service->Release(Tokens[I]); } }
			Test->TestEqual(TEXT("Cancellations preserve other consumers"), Service->GetLeaseCount(), Expected);
			Test->TestEqual(TEXT("No callback during admission"), Delivered, 0);
		}
		~FVisualAssetCommand() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 15) { Test->AddError(TEXT("Visual asset completion timed out")); return true; }
			Service->Tick(0);
			Test->TestTrue(TEXT("GT application count bounded"), Service->GetStats().LastTickApplications <= Service->MaxApplicationsPerTick);
			if (Delivered < Expected) { return false; }
			Test->TestEqual(TEXT("Every surviving consumer completes exactly once"), Delivered, Expected);
			Test->TestEqual(TEXT("One shared async load across owners"), Service->GetStats().Loads, uint64(1));
			for (const auto Token : Tokens) { Service->Release(Token); }
			Test->TestEqual(TEXT("Explicit release unpins assets"), Service->GetGroupCount(), 0);
			Service->OnWorldEndPlay(*World);
			Test->TestEqual(TEXT("Teardown rejects new work"), Service->Request(Owner, FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))), uint64(0));
			Test->AddInfo(FString::Printf(TEXT("admitted=%d delivered=%d loads=%llu max_applications_per_tick=%d"),
				Tokens.Num(), Delivered, Service->GetStats().Loads, Service->MaxApplicationsPerTick));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		UWorld* World;
		AActor* Owner;
		UProject_JVisualAssetSubsystem* Service;
		TArray<uint64> Tokens;
		double Started;
		int32 Delivered = 0;
		int32 Expected = 24;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualAssetsTest, "ProjectJ.Integrated.VisualAssetSharing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualAssetsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVisualAssetCommand(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualPressureTest, "ProjectJ.GroupA.VisualPressure",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualPressureTest::RunTest(const FString& Parameters)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Owner = World->SpawnActor<AActor>(); auto* Other = World->SpawnActor<AActor>();
 auto* S = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
 const FSoftObjectPath Path(TEXT("/Engine/BasicShapes/Cube.Cube"));
 TArray<uint64> Tokens;
 for (int32 I = 0; I < S->MaxLeasesPerOwner; ++I) { Tokens.Add(S->Request(Owner, Path)); }
 TestEqual(TEXT("One owner cannot monopolize global capacity"), S->Request(Owner, Path), uint64(0));
 const auto OtherToken = S->Request(Other, Path);
 TestTrue(TEXT("Another owner retains admission capacity"), OtherToken != 0);
 for (auto Token : Tokens) { S->Release(Token); } S->Release(OtherToken);
 const uint64 Readmitted = S->Request(Owner, Path);
 TestTrue(TEXT("Releasing all owner leases restores admission"), Readmitted != 0); S->Release(Readmitted);
 TestEqual(TEXT("Invalid soft paths never reserve a lease"), S->Request(Owner, FSoftObjectPath()), uint64(0));
 // A bounded failure storm must not make every healthy warm-asset arrival scan all old failures.
 const FSoftObjectPath ActiveFailure(TEXT("/Game/ProjectJVisualNegativeCache/Active.Active"));
 const FSoftObjectPath ExpiredFailure(TEXT("/Game/ProjectJVisualNegativeCache/Expired0.Expired0"));
 const double CacheNow = FPlatformTime::Seconds();
 S->FailedUntil.Add(ActiveFailure, CacheNow + 60.0);
 for (int32 I = 0; I < S->MaxGroups - 1; ++I)
 {
  S->FailedUntil.Add(FSoftObjectPath(FString::Printf(TEXT("/Game/ProjectJVisualNegativeCache/Expired%d.Expired%d"), I, I)), CacheNow);
 }
 const TStrongObjectPtr<UObject> WarmAsset(LoadObject<UObject>(nullptr, *Path.ToString()));
 TestNotNull(TEXT("The real engine asset is resident for warm waiter cancellation"), WarmAsset.Get());
 int32 WarmCallbacks = 0;
 TArray<uint64> WarmTokens;
 for (int32 I = 0; I < 32; ++I)
 {
  WarmTokens.Add(S->Request(Owner, Path, [&](UObject*) { ++WarmCallbacks; }));
  TestTrue(TEXT("Healthy warm arrivals retain admission during a full negative cache"), WarmTokens.Last() != 0);
 }
 TestEqual(TEXT("Healthy admission leaves unrelated failure entries untouched instead of sweeping them per lease"), S->FailedUntil.Num(), S->MaxGroups);
 TestEqual(TEXT("Already resident assets still cannot deliver inline during admission"), WarmCallbacks, 0);
 TestEqual(TEXT("All warm waiters share one normal physical group"), S->GetGroupCount(), 1);
 const uint64 BackoffBefore = S->GetStats().BackoffRejected;
 TestEqual(TEXT("An unexpired failed path still rejects admission"), S->Request(Owner, ActiveFailure), uint64(0));
 TestEqual(TEXT("Active-path rejection retains the existing diagnostic"), S->GetStats().BackoffRejected, BackoffBefore + 1);
 const uint64 ExpiredToken = S->Request(Owner, ExpiredFailure);
 TestTrue(TEXT("A requested path whose expiry is at or before now is readmitted"), ExpiredToken != 0);
 TestFalse(TEXT("Only the requested expired key is removed"), S->FailedUntil.Contains(ExpiredFailure));
 TestEqual(TEXT("Unrelated expired keys remain bounded until failure admission needs space"), S->FailedUntil.Num(), S->MaxGroups - 1);
 S->Release(ExpiredToken);
 for (const uint64 WarmToken : WarmTokens) { S->Release(WarmToken); }
 TestEqual(TEXT("Cancelled warm waiters never run callbacks"), WarmCallbacks, 0);
 TestEqual(TEXT("Cancelling all pre-start warm waiters releases the physical group"), S->GetGroupCount(), 0);
 S->FailedUntil.Add(ExpiredFailure, CacheNow); // Refill the bounded map before a new real timeout is delivered.
 TestEqual(TEXT("Timeout starts with a full bounded negative cache"), S->FailedUntil.Num(), S->MaxGroups);
 int32 Calls = 0;
 const auto Token = S->Request(Owner, Path, [&](UObject* Asset)
 {
  ++Calls; TestNull(TEXT("Expired request delivers null"), Asset);
  TestEqual(TEXT("New failure admission reclaims expired cache entries before its callback"), S->FailedUntil.Num(), 2);
  TestTrue(TEXT("A live unrelated backoff survives capacity reclamation"), S->FailedUntil.Contains(ActiveFailure));
  TestFalse(TEXT("Capacity reclamation removes the old expired path"), S->FailedUntil.Contains(ExpiredFailure));
  TestTrue(TEXT("The newly failed path is recorded before reentrant consumer code"), S->FailedUntil.Contains(Path));
  TestEqual(TEXT("Reentrant failed-path request observes cooldown"), S->Request(Owner, Path), uint64(0));
 });
 // Simulate other failed paths filling the bounded map after this request was admitted.
 // The failure-delivery boundary, rather than an earlier Request sweep, owns the new backoff entry.
 S->FailedUntil.Reset();
 const double DeliveryCacheNow = FPlatformTime::Seconds();
 S->FailedUntil.Add(ActiveFailure, DeliveryCacheNow + 60.0);
 for (int32 I = 0; I < S->MaxGroups - 1; ++I)
 {
  S->FailedUntil.Add(FSoftObjectPath(FString::Printf(TEXT("/Game/ProjectJVisualNegativeCache/Expired%d.Expired%d"), I, I)), DeliveryCacheNow);
 }
 TestEqual(TEXT("Other failures filled the map after the timed-out request's admission"), S->FailedUntil.Num(), S->MaxGroups);
 S->ExpireForTest(Token); S->Tick(0); S->Tick(0);
 TestEqual(TEXT("Timeout delivered once"), Calls, 1);
 TestEqual(TEXT("Timed-out lease released"), S->GetLeaseCount(), 0);
 TestEqual(TEXT("Unfinished load keeps its reservation"), S->GetGroupCount(), 1);
 TestEqual(TEXT("Timeout diagnostic"), S->GetStats().TimedOut, uint64(1));
 // Preserve the existing no-eviction policy when every bounded failure remains active.
 S->FailedUntil.Reset();
 const double ActiveUntil = FPlatformTime::Seconds() + 60.0;
 for (int32 I = 0; I < S->MaxGroups; ++I)
 {
  S->FailedUntil.Add(FSoftObjectPath(FString::Printf(TEXT("/Game/ProjectJVisualNegativeCache/Active%d.Active%d"), I, I)), ActiveUntil);
 }
 const FSoftObjectPath OverflowPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
 const FSoftObjectPath FirstActive(TEXT("/Game/ProjectJVisualNegativeCache/Active0.Active0"));
 int32 OverflowCalls = 0;
 const uint64 OverflowToken = S->Request(Other, OverflowPath, [&](UObject* Asset)
 {
  ++OverflowCalls;
  TestNull(TEXT("The separate capacity fixture delivers the real timeout result"), Asset);
  TestEqual(TEXT("A full live failure cache never exceeds its existing capacity"), S->FailedUntil.Num(), S->MaxGroups);
  TestFalse(TEXT("A fresh failure cannot displace an unexpired cached path"), S->FailedUntil.Contains(OverflowPath));
  TestTrue(TEXT("An existing live cached path remains present"), S->FailedUntil.Contains(FirstActive));
  TestEqual(TEXT("The existing live path still rejects reentrant admission"), S->Request(Other, FirstActive), uint64(0));
 });
 TestTrue(TEXT("A new path may admit even while the failure cache is fully live"), OverflowToken != 0);
 S->ExpireForTest(OverflowToken); S->Tick(0); S->Tick(0);
 TestEqual(TEXT("The full-live-cache timeout callback runs exactly once"), OverflowCalls, 1);
 TestEqual(TEXT("Only existing native unfinished work retains reservations"), S->GetGroupCount(), 2);
 TestEqual(TEXT("Both actual timeout deliveries retain diagnostics"), S->GetStats().TimedOut, uint64(2));
 AddInfo(TEXT("VisualNegativeAdmission warm_burst=32 bounded_failed_entries=256 requested_expiry=1 full_expired_reclaim=1 full_active_no_eviction=1; no per-healthy-request full-cache sweep or CPU/FPS percentage claim."));
 S->OnWorldEndPlay(*World);
 TestEqual(TEXT("World end drains reserved work"), S->GetGroupCount(), 0);
 TestEqual(TEXT("World end releases both live and lazily retained failed-path entries"), S->FailedUntil.Num(), 0);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdVisualAssetsTest, "ProjectJ.Crowd.VisualSharing1024",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrowdVisualAssetsTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FVisualAssetCommand(this, true));
 return true;
}

class FProjectJVisualLeaseChurnCommand : public IAutomationLatentCommand
{
public:
 explicit FProjectJVisualLeaseChurnCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
 {
  World = UWorld::CreateWorld(EWorldType::Game, false);
  GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
  Calls.Init(0, 1024); Cancelled.Init(false, 1024);
  AActor* Owner = nullptr;
  for (int32 Index = 0; Index < 1024; ++Index)
  {
   if (Index % 64 == 0) Owner = World->SpawnActor<AActor>();
   LeaseOwners.Add(Owner);
   Tokens.Add(Service->Request(Owner, Path, [this, Index](UObject* Asset) { Deliver(Index, Asset); }));
   Test->TestTrue(TEXT("Crowd lease admission succeeds"), Tokens.Last() != 0);
  }
  ExtraOwner = World->SpawnActor<AActor>();
  Service->LeaseCursorToken = Tokens[300];
  Retire(100); Test->TestEqual(TEXT("Removing before the cursor preserves its next consumer"), CursorToken(), Tokens[300]);
  Retire(1023); Test->TestEqual(TEXT("Removing a swapped lease preserves the next consumer"), CursorToken(), Tokens[300]);
  Retire(300); Test->TestEqual(TEXT("Removing the next consumer advances to its existing successor"), CursorToken(), Tokens[301]);
  Retire(1020); Test->TestEqual(TEXT("Removing the last dense slot preserves its next consumer"), CursorToken(), Tokens[301]);
  for (int32 Index = 7; Index < 1024; Index += 11) Retire(Index);
  const int32 Before = Service->GetLeaseCount();
  Service->Release(MAX_uint64 - 2);
  Test->TestEqual(TEXT("Unknown cancellation cannot change current leases"), Service->GetLeaseCount(), Before);
  CheckSlots();
 }
 ~FProjectJVisualLeaseChurnCommand() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
 bool Update() override
 {
  if (FPlatformTime::Seconds() - Started > 15) { Test->AddError(TEXT("Visual lease churn completion timed out")); return true; }
  Service->Tick(0); ++Ticks;
  Test->TestTrue(TEXT("Churn retains the application budget"), Service->GetStats().LastTickApplications <= Service->MaxApplicationsPerTick);
  Test->TestTrue(TEXT("Churn retains the visit budget"), Service->GetStats().LastTickLeaseVisits <= Service->MaxLeaseVisitsPerTick);
  CheckSlots();
  int32 Expected = 0;
  for (int32 Index = 0; Index < Calls.Num(); ++Index) Expected += !Cancelled[Index];
  if (Delivered < Expected) return false;
  Test->TestTrue(TEXT("Actual completion exercises reentrant release and admission"), bReentered);
  Test->TestTrue(TEXT("Existing survivors are visited within their fixed ready-frontier bounds despite continuous churn"),
   bProbeVisited && bChurnProbeVisited && ChurnSteps > 8);
  for (int32 Index = 0; Index < Calls.Num(); ++Index)
   Test->TestEqual(TEXT("Each surviving consumer completes once and retired consumers never run"), Calls[Index], Cancelled[Index] ? 0 : 1);
  Test->TestEqual(TEXT("Crowd churn continues sharing one physical asset load"), Service->GetStats().Loads, uint64(1));
  for (int32 Index = Tokens.Num() - 1; Index >= 0; --Index) Service->Release(Tokens[Index]);
  Test->TestEqual(TEXT("Retirement drains the token index"), Service->LeaseSlots.Num(), 0);
  Test->TestEqual(TEXT("Retirement releases the shared resident group"), Service->GetGroupCount(), 0);
  Test->TestEqual(TEXT("Retirement returns all per-owner quotas"), Service->OwnerLeaseCounts.Num(), 0);
  Test->TestEqual(TEXT("Empty storage normalizes the token cursor"), Service->LeaseCursorToken, uint64(0));
  Service->NextToken = 0;
  const uint64 First = Service->Request(ExtraOwner, Path);
  Service->NextToken = MAX_uint64;
  const uint64 Wrapped = Service->Request(ExtraOwner, Path);
  Test->TestEqual(TEXT("Counter fixture retains its first live nonzero token"), First, uint64(1));
  Test->TestEqual(TEXT("Counter wrap skips zero and an existing live token"), Wrapped, uint64(2));
  Service->Release(First); Service->Release(First); Service->Release(Wrapped);
  Test->TestEqual(TEXT("Wrapped tokens retire independently and idempotently"), Service->GetLeaseCount(), 0);
  Service->OnWorldEndPlay(*World);
  Test->TestEqual(TEXT("World retirement leaves no token index"), Service->LeaseSlots.Num(), 0);
  Test->AddInfo(FString::Printf(TEXT("VisualLeaseChurn admitted=%d delivered=%d cancelled=%d shared_loads=1 ticks=%d churn_steps=%d ready_frontier=%d probe_bound=%d probe_visit=%d churn_probe=%d churn_probe_bound=%d churn_probe_visit=%d"),
   Calls.Num(), Delivered, Calls.Num() - Expected, Ticks, ChurnSteps, ReadyFrontierIndex, ProbeVisitBound, ProbeVisit,
   ChurnProbeIndex, ChurnProbeVisitBound, ChurnProbeVisit));
  return true;
 }
private:
 uint64 TokenAtSlot(int32 Slot) const
 {
  for (const auto& Pair : Service->LeaseSlots) if (Pair.Value == Slot) return Pair.Key;
  return 0;
 }
 uint64 CursorToken() const { return Service->LeaseCursorToken; }
 void Retire(int32 Index)
 {
  if (Calls[Index] == 0) Cancelled[Index] = true;
  Service->Release(Tokens[Index]);
 }
 void RetireToken(uint64 Token)
 {
  const int32 Index = Tokens.IndexOfByKey(Token);
  if (Index != INDEX_NONE && !IsProtected(Index)) Retire(Index);
 }
 bool IsProtected(int32 Index) const { return Index == ProbeIndex || Index == ChurnProbeIndex; }
 void CheckSlots()
 {
  Test->TestEqual(TEXT("Live tokens and dense leases have equal cardinality"), Service->LeaseSlots.Num(), Service->GetLeaseCount());
  TSet<int32> Seen;
  for (const auto& Pair : Service->LeaseSlots)
  {
   Test->TestTrue(TEXT("Every live token resolves a unique in-range slot"),
    Service->Leases.IsValidIndex(Pair.Value) && !Seen.Contains(Pair.Value));
   Seen.Add(Pair.Value);
  }
  Test->TestTrue(TEXT("Cursor remains current or normalized when empty"), Service->GetLeaseCount()
   ? Service->LeaseSlots.Contains(Service->LeaseCursorToken) : Service->LeaseCursorToken == 0);
 }
 void AdmitReplacement(AActor* Owner)
 {
  const int32 NewIndex = Calls.Num();
  Calls.Add(0); Cancelled.Add(false); LeaseOwners.Add(Owner);
  const int32 Before = Delivered;
  Tokens.Add(Service->Request(Owner, Path, [this, NewIndex](UObject* NewAsset) { Deliver(NewIndex, NewAsset); }));
  Test->TestTrue(TEXT("Reentrant new admission succeeds after retirement"), Tokens.Last() != 0);
  Test->TestEqual(TEXT("Reentrant admission still cannot invoke inline completion"), Delivered, Before);
 }
 void Deliver(int32 Index, UObject* Asset)
 {
  Test->TestNotNull(TEXT("Churning consumers receive the real shared asset"), Asset);
  Test->TestFalse(TEXT("A retired consumer cannot receive completion"), Cancelled[Index]);
  if (ReadyFrontierIndex == INDEX_NONE)
  {
   // Pending groups still participate in normal bounded visits. Freeze the
   // frontier when this one shared group first becomes ready, not at admission.
   ReadyFrontierIndex = Index;
   int32 Prefix = 0;
   for (int32 Offset = 0; Offset < 1024; ++Offset)
   {
    const int32 Existing = (ReadyFrontierIndex + Offset) % 1024;
    if (Cancelled[Existing]) continue;
    ++Prefix;
    if (Existing == ProbeIndex) ProbeVisitBound = Prefix;
    if (Prefix == ChurnProbeVisitBound) ChurnProbeIndex = Existing;
   }
   Test->TestTrue(TEXT("Both protected consumers belong to the frozen original ready frontier"),
    ProbeVisitBound > 0 && ChurnProbeIndex != INDEX_NONE);
  }
  ++Calls[Index]; ++Delivered;
  if (Index < 1024)
  {
   const int32 Order = (Index - ReadyFrontierIndex + 1024) % 1024;
   Test->TestTrue(TEXT("Ready surviving original consumers retain their round-robin order"), Order > LastOriginalOrder);
   LastOriginalOrder = Order;
  }
  else Test->TestTrue(TEXT("New tail admissions cannot overtake the older protected consumers"), bProbeVisited && bChurnProbeVisited);
  if (Index == ProbeIndex)
  {
   bProbeVisited = true; ProbeVisit = Delivered;
   Test->TestTrue(TEXT("Protected consumer cannot be postponed by dense-slot churn or the application boundary"), ProbeVisit <= ProbeVisitBound);
  }
  if (Index == ChurnProbeIndex)
  {
   bChurnProbeVisited = true; ChurnProbeVisit = Delivered;
   Test->TestTrue(TEXT("A fixed ready prefix retains its strict visit bound under continuous churn"), ChurnProbeVisit <= ChurnProbeVisitBound);
  }
  if (!bReentered)
  {
   bReentered = true;
   Retire(Index);
   RetireToken(CursorToken());
   RetireToken(TokenAtSlot(0));
   RetireToken(TokenAtSlot(Service->GetLeaseCount() - 1));
   AdmitReplacement(ExtraOwner);
   CheckSlots();
  }
  else if (!bChurnProbeVisited)
  {
   // Continuously replace a dense front slot while newer tail leases are live.
   // Storage movement must not let these new arrivals overtake older ready work.
   Retire(Index);
   RetireToken(CursorToken());
   int32 Victim = Tokens.IndexOfByKey(TokenAtSlot(0));
   if (IsProtected(Victim)) Victim = Tokens.IndexOfByKey(TokenAtSlot(Service->GetLeaseCount() - 1));
   if (Victim != INDEX_NONE && !IsProtected(Victim))
   {
    Retire(Victim); AdmitReplacement(LeaseOwners[Victim]); ++ChurnSteps;
   }
  }
 }
 FAutomationTestBase* Test;
 UWorld* World;
 AActor* ExtraOwner;
 UProject_JVisualAssetSubsystem* Service;
 FSoftObjectPath Path{TEXT("/Engine/BasicShapes/Cube.Cube")};
 TArray<uint64> Tokens;
 TArray<AActor*> LeaseOwners;
 TArray<int32> Calls;
 TArray<bool> Cancelled;
 double Started;
 static constexpr int32 ProbeIndex = 400;
 static constexpr int32 ChurnProbeVisitBound = 91;
 int32 Delivered = 0, Ticks = 0, LastOriginalOrder = -1, ChurnSteps = 0;
 int32 ProbeVisitBound = 0, ProbeVisit = 0;
 int32 ReadyFrontierIndex = INDEX_NONE, ChurnProbeIndex = INDEX_NONE, ChurnProbeVisit = 0;
 bool bReentered = false, bProbeVisited = false, bChurnProbeVisited = false;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualLeaseChurnTest, "ProjectJ.Foundation.VisualAssets.LeaseChurn1024",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualLeaseChurnTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FProjectJVisualLeaseChurnCommand(this));
 return true;
}
#endif
