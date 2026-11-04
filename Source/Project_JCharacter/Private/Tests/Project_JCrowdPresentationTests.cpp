#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/Project_JWeaponPresentationActor.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JSpellbladeCharacter.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace
{
class FCrowdEquipmentFrameCommand : public IAutomationLatentCommand
{
public:
	explicit FCrowdEquipmentFrameCommand(TFunction<bool()> InBody) : Body(MoveTemp(InBody)) {}
	bool Update() override { return Body(); }
private:
	TFunction<bool()> Body;
};
struct FCrowdEquipmentFrames
{
	TStrongObjectPtr<UClass> WeaponClass;
	TStrongObjectPtr<USkeletalMesh> Mesh;
	TStrongObjectPtr<UWorld> World;
	TStrongObjectPtr<UProject_JWeaponPresentationProfile> Profile;
	UProject_JPresentationBudgetSubsystem* Budget = nullptr;
	TArray<AProject_JSpellbladeCharacter*> Pawns;
	TArray<UProject_JWeaponPresentationComponent*> Presentations;
	TArray<TStrongObjectPtr<UProject_JWeaponPresentationComponent>> CapacityOwners;
	FString CSV = TEXT("mode,step,applications,milliseconds\n");
	int32 Mode = 0, Stage = 0, Step = 0, CapacityDrainTicks = 0, ExhaustedSameFrameChecks = 0;
	uint64 LastUpdateFrame = MAX_uint64, CapacityQueuedFrame = 0, Region = 0;
	double StartedAt = FPlatformTime::Seconds();
	bool bRegionOpen = false;
	void RetireWorld()
	{
		for (const auto& Component : CapacityOwners) { Component->DestroyComponent(); }
		CapacityOwners.Reset(); Pawns.Reset(); Presentations.Reset(); Profile.Reset(); Budget = nullptr;
		if (bRegionOpen) { TRACE_END_REGION_WITH_ID(Region); bRegionOpen = false; }
		if (World)
		{
			auto* Retiring = World.Get(); Retiring->DestroyWorld(false); GEngine->DestroyWorldContext(Retiring); World.Reset();
		}
	}
	~FCrowdEquipmentFrames() { RetireWorld(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdPresentationTest, "ProjectJ.Crowd.RemoteEquipmentBurst",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJCrowdPresentationTest::RunTest(const FString&)
{
	auto Run = MakeShared<FCrowdEquipmentFrames>();
	Run->WeaponClass.Reset(LoadClass<AProject_JWeaponPresentationActor>(nullptr,
		TEXT("/Game/Character_BPs/GreatSword/BP_Greatsword_Basic.BP_Greatsword_Basic_C")));
	if (!TestNotNull(TEXT("Authored concrete weapon class"), Run->WeaponClass.Get()) ||
		!TestFalse(TEXT("Authored weapon can be instantiated"), Run->WeaponClass->HasAnyClassFlags(CLASS_Abstract))) { return false; }
	Run->Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	if (!TestNotNull(TEXT("Authored mesh supplies a valid root attachment"), Run->Mesh.Get())) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCrowdEquipmentFrameCommand([this, Run]()
	{
		// Yield to the engine rather than inventing frames or replenishing a per-Tick allowance.
		if (Run->LastUpdateFrame == GFrameCounter) { return false; }
		Run->LastUpdateFrame = GFrameCounter;
		if (FPlatformTime::Seconds() - Run->StartedAt >= 120)
		{ AddError(TEXT("Natural-frame equipment burst exceeded its bounded fixture deadline")); Run->RetireWorld(); return true; }
		const bool bBudget = Run->Mode == 1;
		if (Run->Stage == 0)
		{
			Run->World.Reset(UWorld::CreateWorld(EWorldType::Game, false));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Run->World.Get());
			Run->Budget = Run->World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
			if (!TestNotNull(TEXT("Actual world presentation budget"), Run->Budget)) { Run->RetireWorld(); return true; }
			Run->Profile.Reset(NewObject<UProject_JWeaponPresentationProfile>(Run->World.Get()));
			Run->Profile->WeaponActorClass = Run->WeaponClass.Get();
			Run->Profile->SheathedSocketName = Run->Profile->DrawnSocketName = TEXT("root");
			Run->Profile->bPreferVisualFollowerSockets = false;
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			for (int32 I = 0; I < 300; ++I)
			{
				auto* Pawn = Run->World->SpawnActor<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(), FVector(I * 100, 0, 0), FRotator::ZeroRotator, Params);
				if (!TestNotNull(TEXT("Concrete crowd presentation pawn"), Pawn)) { Run->RetireWorld(); return true; }
				Pawn->GetMesh()->SetSkeletalMesh(Run->Mesh.Get());
				auto* P = Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
				P->bForceBudgetForTest = bBudget; Run->Pawns.Add(Pawn); Run->Presentations.Add(P);
			}
			const double Started = FPlatformTime::Seconds();
			Run->Region = TRACE_BEGIN_REGION_WITH_ID(bBudget ? TEXT("ProjectJ.Crowd.Equipment.Budget") : TEXT("ProjectJ.Crowd.Equipment.Immediate"));
			Run->bRegionOpen = true;
			for (auto* Pawn : Run->Pawns) { Pawn->SetCurrentWeaponPresentationProfile(Run->Profile.Get()); }
			Run->CSV += FString::Printf(TEXT("%s,enqueue,300,%.6f\n"), bBudget ? TEXT("Budget") : TEXT("Immediate"), (FPlatformTime::Seconds() - Started) * 1000);
			if (bBudget)
			{
				TestEqual(TEXT("All visible arrivals queue independently"), Run->Budget->GetStats().Pending, 300);
				const uint64 OldRevision = Run->Presentations[0]->PresentationRevision;
				for (auto* P : Run->Presentations) { P->RefreshPresentation(); P->RefreshPresentation(); }
				TestEqual(TEXT("Repeated replication refresh coalesces"), Run->Budget->GetStats().Pending, 300);
				Run->Presentations[0]->ApplyBudgetedPresentation(OldRevision);
				TestNull(TEXT("Old request cannot create a weapon"), Run->Presentations[0]->GetSpawnedWeapon());
				Run->Presentations[1]->AttachWeaponToDrawnSocket();
				TestNotNull(TEXT("Draw notify bypasses queue without losing its frame"), Run->Presentations[1]->GetSpawnedWeapon());
				Run->Pawns[0]->SetCurrentWeaponPresentationProfile(nullptr); Run->Pawns[0]->SetCurrentWeaponPresentationProfile(Run->Profile.Get());
				Run->Budget->Tick(0);
				TestNull(TEXT("Cancelled and resubmitted owner cannot jump ahead using its obsolete FIFO slot"), Run->Presentations[0]->GetSpawnedWeapon());
			}
			for (int32 I = 0; I < 300; I += 10) { Run->Pawns[I]->SetCurrentWeaponPresentationProfile(nullptr); }
			Run->Stage = 1; Run->Step = 0;
			return false;
		}
		auto* Budget = Run->Budget;
		const auto PumpBudget = [this, &Run, Budget]()
		{
			Budget->Tick(0);
			const auto First = Budget->GetStats();
			TestEqual(TEXT("Physical applications use the current real engine frame"), First.Frame, uint64(GFrameCounter));
			TestTrue(TEXT("Cosmetic creation count bounded per frame"), First.FrameApplications <= Budget->MaxApplications);
			TestTrue(TEXT("Cosmetic visit count bounded per frame"), First.FrameVisits <= 128);
			const bool bExhausted = First.FrameApplications == Budget->MaxApplications || First.FrameVisits == 128 || First.FrameQueueMilliseconds >= 1.;
			// A second call may use a still-unspent fraction, but never fresh credits.
			Budget->Tick(0);
			const auto Repeated = Budget->GetStats();
			TestEqual(TEXT("Repeated Tick stays in the same real frame"), Repeated.Frame, First.Frame);
			TestTrue(TEXT("Repeated Tick cannot refill the shared four-application allowance"), Repeated.FrameApplications <= Budget->MaxApplications
				&& Repeated.FrameApplications >= First.FrameApplications && Repeated.Applied >= First.Applied
				&& Repeated.Applied - First.Applied <= uint64(Budget->MaxApplications - First.FrameApplications));
			TestTrue(TEXT("Repeated Tick cannot reset visit/time accounting"), Repeated.FrameVisits >= First.FrameVisits
				&& Repeated.FrameVisits <= 128 && Repeated.FrameQueueMilliseconds >= First.FrameQueueMilliseconds);
			if (bExhausted)
			{
				TestEqual(TEXT("Exhausted same-frame Tick cannot gain another application"), Repeated.Applied, First.Applied);
				++Run->ExhaustedSameFrameChecks;
			}
			return First;
		};
		if (Run->Stage == 1)
		{
			if (Budget->IsTickable())
			{
				const auto Applied = PumpBudget();
				TestTrue(TEXT("Cosmetic creation count bounded per tick"), Applied.LastApplications <= Budget->MaxApplications);
				Run->CSV += FString::Printf(TEXT("Budget,%d,%d,%.6f\n"), Run->Step++, Applied.LastApplications, Applied.LastMilliseconds);
				if (Budget->IsTickable())
				{
					if (Run->Step >= 1000) { AddError(TEXT("Existing crowd queue did not drain within 1000 natural frames")); Run->RetireWorld(); return true; }
					return false;
				}
			}
			for (int32 I = 0; I < 300; ++I)
			{
				TestTrue(TEXT("All surviving revisions converge; revoked weapons stay absent"), (Run->Presentations[I]->GetSpawnedWeapon() != nullptr) == (I % 10 != 0));
				Run->Pawns[I]->SetCurrentWeaponPresentationProfile(nullptr);
			}
			TestEqual(TEXT("Queue drains without starvation"), Budget->GetStats().Pending, 0);
			if (bBudget)
			{
				// Actual rejection/readmission is unchanged. Only the pre-existing retry due time is deterministic.
				auto* RetryTarget = Run->Presentations[2]; RetryTarget->bForceBudgetForTest = false;
				Run->Pawns[2]->SetCurrentWeaponPresentationProfile(Run->Profile.Get());
				TestNotNull(TEXT("Retry fixture initially uses the normal local synchronous path"), RetryTarget->GetSpawnedWeapon());
				RetryTarget->DestroyWeaponPresentation();
				Run->CapacityOwners.Reserve(Budget->MaxPending);
				for (int32 CapacityIndex = 0; CapacityIndex < Budget->MaxPending; ++CapacityIndex)
				{
					auto* CapacityComponent = NewObject<UProject_JWeaponPresentationComponent>(Run->Pawns[2]);
					Run->CapacityOwners.Emplace(CapacityComponent);
					TestTrue(TEXT("Existing bounded queue admits its capacity"), Budget->Request(CapacityComponent, 1));
				}
				TestEqual(TEXT("Actual pending queue is full before rejection"), Budget->GetStats().Pending, Budget->MaxPending);
				const uint64 BeforeRejected = Budget->GetStats().Rejected;
				RetryTarget->QueueBudgetedPresentation();
				TestTrue(TEXT("Rejected current request arms the existing retry"), RetryTarget->bRetryBudget);
				TestTrue(TEXT("Actual queue rejection is observed"), Budget->GetStats().Rejected > BeforeRejected);
				RetryTarget->NextBudgetRetry = 0;
				RetryTarget->TickComponent(0, LEVELTICK_All, &RetryTarget->PrimaryComponentTick);
				TestNull(TEXT("Retry cannot bypass full admission through the local immediate path"), RetryTarget->GetSpawnedWeapon());
				TestEqual(TEXT("Rejected retry preserves the queue capacity bound"), Budget->GetStats().Pending, Budget->MaxPending);
				TestTrue(TEXT("Still-rejected retry remains scheduled by the existing policy"), RetryTarget->bRetryBudget);
				for (const auto& Component : Run->CapacityOwners) { Budget->Cancel(Component.Get()); }
				TestEqual(TEXT("Cancelling capacity holders restores admission"), Budget->GetStats().Pending, 0);
				RetryTarget->NextBudgetRetry = 0;
				RetryTarget->TickComponent(0, LEVELTICK_All, &RetryTarget->PrimaryComponentTick);
				TestFalse(TEXT("Successful readmission clears rejection retry"), RetryTarget->bRetryBudget);
				TestNull(TEXT("Readmission alone does not synchronously create a weapon"), RetryTarget->GetSpawnedWeapon());
				TestEqual(TEXT("Only the latest real consumer is queued"), Budget->GetStats().Pending, 1);
				Run->Stage = 2; Run->CapacityDrainTicks = 0; Run->CapacityQueuedFrame = GFrameCounter;
				return false;
			}
			Run->Stage = 3;
		}
		if (Run->Stage == 2)
		{
			const uint64 CapacityFrames = GFrameCounter - Run->CapacityQueuedFrame;
			if (Budget->IsTickable())
			{
				PumpBudget(); ++Run->CapacityDrainTicks;
				TestTrue(TEXT("Readmitted physical work retains the existing apply count"), Budget->GetStats().LastApplications <= Budget->MaxApplications);
				if (Budget->IsTickable())
				{
					if (CapacityFrames >= 256) { AddError(TEXT("Cancelled FIFO/readmission did not drain within 256 natural frames")); Run->RetireWorld(); return true; }
					return false;
				}
			}
			TestEqual(TEXT("Cancelled FIFO slots and current admission drain"), Budget->GetStats().Pending, 0);
			// The real world subsystem may also Tick between latent observations.
			// Count elapsed real frames rather than treating those applications as manual calls.
			TestTrue(TEXT("Cancelled capacity FIFO is consumed across bounded visits"), CapacityFrames >= 32 && CapacityFrames < 256);
			TestNotNull(TEXT("Latest consumer eventually creates its weapon through actual budget apply"), Run->Presentations[2]->GetSpawnedWeapon());
			TestTrue(TEXT("Real exhausted frame boundaries exercise repeated-Tick rejection"), Run->ExhaustedSameFrameChecks > 0);
			Run->Pawns[2]->SetCurrentWeaponPresentationProfile(nullptr);
			for (const auto& Component : Run->CapacityOwners) { Component->DestroyComponent(); }
			Run->CapacityOwners.Reset(); Run->Stage = 3;
		}
		if (Run->Stage == 3)
		{
			Run->Pawns[0]->SetCurrentWeaponPresentationProfile(Run->Profile.Get()); Run->Pawns[0]->Destroy(); Run->Presentations[0]->RefreshPresentation();
			Budget->Tick(0); TestNull(TEXT("Destroy cannot resurrect a queued weapon"), Run->Presentations[0]->GetSpawnedWeapon());
			Budget->OnWorldEndPlay(*Run->World);
			TestEqual(TEXT("World end clears pending requests"), Budget->GetStats().Pending, 0);
			TestFalse(TEXT("World end rejects new cosmetic work"), Budget->Request(Run->Presentations[2], 1));
			Run->RetireWorld();
			if (++Run->Mode < 2) { Run->Stage = 0; return false; }
			FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
			if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/CrowdE_20260910/Metrics"); }
			IFileManager::Get().MakeDirectory(*Output, true);
			TestTrue(TEXT("Remote creation burst timings recorded"), FFileHelper::SaveStringToFile(Run->CSV, *(Output / TEXT("equipment-burst.csv"))));
			return true;
		}
		return false;
	}));
	return true;
}
#endif
