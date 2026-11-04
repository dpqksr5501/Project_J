#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

class FProjectJVisualResidencyCommand : public IAutomationLatentCommand
{
public:
	explicit FProjectJVisualResidencyCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
		for (int32 O = 0; O < 3; ++O)
		{
			auto* Owner = World->SpawnActor<AActor>(); Owners.Add(Owner);
			for (int32 I = 0; I < 64; ++I)
				Service->Request(Owner, FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")), [this](UObject* Asset)
				{ Test->TestNotNull(TEXT("Real shared async asset resolves"), Asset); ++Delivered; });
		}
	}
	~FProjectJVisualResidencyCommand() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 15) { Test->AddError(TEXT("Resident lifecycle test timed out")); return true; }
		Service->Tick(0);
		if (Stage == 0 && Delivered == 192)
		{
			Test->TestEqual(TEXT("Resident leases still pin their shared group"), Service->GetLeaseCount(), 192);
			Test->TestEqual(TEXT("All pending applications completed"), Service->PendingLeases, 0);
			Test->TestFalse(TEXT("Resident-only service sleeps before maintenance"), Service->IsTickable());
			const uint64 Ticks = Service->GetStats().ExecutedTicks;
			for (int32 I = 0; I < 100; ++I) Service->Tick(0);
			Test->TestEqual(TEXT("Idle manual calls do not scan resident leases"), Service->GetStats().ExecutedTicks, Ticks);
			NewToken = Service->Request(Owners[0], FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")), [this](UObject* Asset)
			{ Test->TestNotNull(TEXT("A resident group remains reusable"), Asset); ++Delivered; });
			Test->TestEqual(TEXT("Per-owner admission still protects the resident owner limit"), NewToken, uint64(0));
			ExtraOwner = World->SpawnActor<AActor>();
			NewToken = Service->Request(ExtraOwner, FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")), [this](UObject* Asset)
			{ Test->TestNotNull(TEXT("Resident callback runs on resumed service"), Asset); ++Delivered; });
			Test->TestTrue(TEXT("New pending consumer wakes immediately"), NewToken != 0 && Service->IsTickable());
			Test->TestEqual(TEXT("No inline completion on admission"), Delivered, 192);
			Stage = 1;
		}
		if (Stage != 1 || Delivered != 193) return false;
		Service->Release(NewToken);
		Test->TestEqual(TEXT("Resident reuse does not start another load"), Service->GetStats().Loads, uint64(1));
		for (auto* Owner : Owners) Owner->Destroy();
		Service->NextIdleSweep = 0; // Deterministic due-time injection, not a real half-second delay.
		int32 SweepTicks = 0;
		while (Service->IsTickable() && SweepTicks < 64)
		{
			Service->Tick(0); ++SweepTicks;
			Test->TestTrue(TEXT("Maintenance preserves per-tick visit bound"), Service->GetStats().LastTickLeaseVisits <= Service->MaxLeaseVisitsPerTick);
		}
		Test->TestEqual(TEXT("Every destroyed weak owner releases residency"), Service->GetLeaseCount(), 0);
		Test->TestEqual(TEXT("Last resident releases the load handle/group"), Service->GetGroupCount(), 0);
		Test->TestTrue(TEXT("Sweep continues across ticks rather than waiting between chunks"), SweepTicks >= 2 && SweepTicks < 64);
		Service->Request(ExtraOwner, FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")), [this](UObject*) { ++LateCalls; });
		Service->Tick(0); Service->OnWorldEndPlay(*World); Service->Tick(0);
		Test->TestEqual(TEXT("Teardown clears pending admission"), Service->PendingLeases, 0);
		Test->TestEqual(TEXT("No completion after retirement"), LateCalls, 0);
		Test->TestFalse(TEXT("Retired service stays asleep"), Service->IsTickable());
		Test->AddInfo(FString::Printf(TEXT("VisualResidency residents=192 idle_manual_calls=100 idle_executed_ticks=0 sweep_ticks=%d"), SweepTicks));
		return true;
	}
private:
	FAutomationTestBase* Test;
	UWorld* World;
	UProject_JVisualAssetSubsystem* Service;
	TArray<AActor*> Owners;
	AActor* ExtraOwner = nullptr;
	double Started;
	uint64 NewToken = 0;
	int32 Stage = 0, Delivered = 0, LateCalls = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualResidencyTest, "ProjectJ.Foundation.VisualAssets.ResidentMaintenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualResidencyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FProjectJVisualResidencyCommand(this));
	return true;
}
#endif
