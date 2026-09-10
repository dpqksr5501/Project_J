#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Project_JGreatswordCharacter.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/MiscTrace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdPresentationTest, "ProjectJ.Crowd.RemoteEquipmentBurst",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrowdPresentationTest::RunTest(const FString&)
{
	FString CSV = TEXT("mode,step,applications,milliseconds\n");
	for (const bool bBudget : {false, true})
	{
		auto* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* Budget = World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		auto* Profile = NewObject<UProject_JWeaponPresentationProfile>(World);
		Profile->WeaponActorClass = AStaticMeshActor::StaticClass();
		Profile->SheathedSocketName = Profile->DrawnSocketName = NAME_None;
		TArray<AProject_JGreatswordCharacter*> Pawns;
		TArray<UProject_JWeaponPresentationComponent*> Presentations;
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < 300; ++I)
		{
			auto* Pawn = World->SpawnActor<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(), FVector(I * 100, 0, 0), FRotator::ZeroRotator, Params);
			auto* P = Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
			P->bForceBudgetForTest = bBudget; Pawns.Add(Pawn); Presentations.Add(P);
		}
		const double Started = FPlatformTime::Seconds();
		const uint64 Region = TRACE_BEGIN_REGION_WITH_ID(bBudget ? TEXT("ProjectJ.Crowd.Equipment.Budget") : TEXT("ProjectJ.Crowd.Equipment.Immediate"));
		for (auto* Pawn : Pawns) { Pawn->SetCurrentWeaponPresentationProfile(Profile); }
		CSV += FString::Printf(TEXT("%s,enqueue,300,%.6f\n"), bBudget ? TEXT("Budget") : TEXT("Immediate"), (FPlatformTime::Seconds() - Started) * 1000);
		if (bBudget)
		{
			TestEqual(TEXT("All visible arrivals queue independently"), Budget->GetStats().Pending, 300);
			const uint64 OldRevision = Presentations[0]->PresentationRevision;
			for (auto* P : Presentations) { P->RefreshPresentation(); P->RefreshPresentation(); }
			TestEqual(TEXT("Repeated replication refresh coalesces"), Budget->GetStats().Pending, 300);
			Presentations[0]->ApplyBudgetedPresentation(OldRevision);
			TestNull(TEXT("Old request cannot create a weapon"), Presentations[0]->GetSpawnedWeapon());
			Presentations[1]->AttachWeaponToDrawnSocket();
			TestNotNull(TEXT("Draw notify bypasses queue without losing its frame"), Presentations[1]->GetSpawnedWeapon());
			Pawns[0]->SetCurrentWeaponPresentationProfile(nullptr); Pawns[0]->SetCurrentWeaponPresentationProfile(Profile);
			Budget->Tick(0);
			TestNull(TEXT("Cancelled and resubmitted owner cannot jump ahead using its obsolete FIFO slot"), Presentations[0]->GetSpawnedWeapon());
		}
		for (int32 I = 0; I < 300; I += 10) { Pawns[I]->SetCurrentWeaponPresentationProfile(nullptr); }
		for (int32 Step = 0; Budget->IsTickable() && Step < 1000; ++Step)
		{
			Budget->Tick(0);
			TestTrue(TEXT("Cosmetic creation count bounded per tick"), Budget->GetStats().LastApplications <= Budget->MaxApplications);
			CSV += FString::Printf(TEXT("Budget,%d,%d,%.6f\n"), Step, Budget->GetStats().LastApplications, Budget->GetStats().LastMilliseconds);
		}
		for (int32 I = 0; I < 300; ++I)
		{
			TestTrue(TEXT("All surviving revisions converge; revoked weapons stay absent"), (Presentations[I]->GetSpawnedWeapon() != nullptr) == (I % 10 != 0));
			Pawns[I]->SetCurrentWeaponPresentationProfile(nullptr);
		}
		TestEqual(TEXT("Queue drains without starvation"), Budget->GetStats().Pending, 0);
		Pawns[0]->SetCurrentWeaponPresentationProfile(Profile); Pawns[0]->Destroy(); Presentations[0]->RefreshPresentation();
		Budget->Tick(0); TestNull(TEXT("Destroy cannot resurrect a queued weapon"), Presentations[0]->GetSpawnedWeapon());
		Budget->OnWorldEndPlay(*World);
		TestEqual(TEXT("World end clears pending requests"), Budget->GetStats().Pending, 0);
		TestFalse(TEXT("World end rejects new cosmetic work"), Budget->Request(Presentations[2], 1));
		TRACE_END_REGION_WITH_ID(Region);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/CrowdE_20260910/Metrics"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	TestTrue(TEXT("Remote creation burst timings recorded"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("equipment-burst.csv"))));
	return true;
}
#endif
