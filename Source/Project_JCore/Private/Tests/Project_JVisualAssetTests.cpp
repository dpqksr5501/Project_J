#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
namespace
{
	class FVisualAssetCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVisualAssetCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds())
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Owner = World->SpawnActor<AActor>(); Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
			const FSoftObjectPath Path(TEXT("/Engine/BasicShapes/Cube.Cube"));
			for (int32 I = 0; I < 25; ++I)
			{
				Tokens.Add(Service->Request(Owner, Path, [this](UObject* Asset)
				{
					Test->TestNotNull(TEXT("Async engine fixture resolves"), Asset); ++Delivered;
				}));
			}
			Test->TestEqual(TEXT("Identical assets share one group"), Service->GetGroupCount(), 1);
			Service->Release(Tokens[0]);
			Test->TestEqual(TEXT("One cancellation preserves other consumers"), Service->GetLeaseCount(), 24);
			Test->TestEqual(TEXT("No callback during admission"), Delivered, 0);
		}
		~FVisualAssetCommand() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 15) { Test->AddError(TEXT("Visual asset completion timed out")); return true; }
			Service->Tick(0);
			Test->TestTrue(TEXT("GT application count bounded"), Service->GetStats().LastTickApplications <= Service->MaxApplicationsPerTick);
			if (Delivered < 24) { return false; }
			Test->TestEqual(TEXT("One async load for 25 requests"), Service->GetStats().Loads, uint64(1));
			for (const auto Token : Tokens) { Service->Release(Token); }
			Test->TestEqual(TEXT("Explicit release unpins assets"), Service->GetGroupCount(), 0);
			Service->OnWorldEndPlay(*World);
			Test->TestEqual(TEXT("Teardown rejects new work"), Service->Request(Owner, FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"))), uint64(0));
			Test->AddInfo(FString::Printf(TEXT("25 admitted, 1 cancelled, 24 delivered; loads=%llu max_applications_per_tick=%d"),
				Service->GetStats().Loads, Service->MaxApplicationsPerTick));
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
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualAssetsTest, "ProjectJ.Integrated.VisualAssetSharing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualAssetsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVisualAssetCommand(this));
	return true;
}
#endif
