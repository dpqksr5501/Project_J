#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JPlayerState.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "Project_JAbilitySystemComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "UObject/StrongObjectPtr.h"
using namespace ProjectJ::MMO;
namespace
{
struct FCorruptResult
{
	bool OK = false;
	int64 Revision = 0;
	TArray<uint8> Payload;
};
class FProfileFailureCases : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World;
	UProject_JLocalProfileSubsystem *Profiles;
	FString File;
	TWeakObjectPtr<AProject_JPlayerState> Old, Current;
	int32 Step = 0;
	bool Done = false, Success = false;
	double Started = FPlatformTime::Seconds();
	TFuture<FCorruptResult> Corrupt;
	TFuture<bool> Preserved;
	FCorruptResult Expected;
	void Open()
	{
		auto *PS = World->SpawnActor<AProject_JPlayerState>();
		Current = PS;
		Done = Success = false;
		Profiles->OpenProfile(PS, TEXT("FailureFixture"), [this](bool OK) {
			Done = true;
			Success = OK;
		});
	}
	void CorruptDefinition(FName Id, int32 Version)
	{
		const FGuid Character = Current->GetCharacterId().Value;
		Profiles->Detach(Current.Get());
		Corrupt = Async(EAsyncExecution::ThreadPool, [File = File, Character, Id, Version] {
			FCorruptResult Out;
			FProject_JSQLiteRepository DB(File);
			FRecord R;
			FProject_JLocalProfileSnapshot Snapshot;
			const FAggregateKey Key{TEXT("LocalProfile"), Character};
			if (DB.Read(Key, R) && FProject_JLocalProfileSnapshot::Decode(R.Payload, Snapshot) &&
				Snapshot.Progression.Experience == 9)
			{
				Snapshot.Items.Reset();
				FProject_JPersistentItem Item;
				Item.Instance = FGuid::NewGuid();
				Item.Definition = Id;
				Item.DefinitionVersion = Version;
				Item.Order = 0;
				Snapshot.Items.Add(Item);
				if (Snapshot.Encode(Out.Payload))
				{
					const auto Result = DB.Commit({FGuid::NewGuid(),
												   TEXT("OwnedInvalidDefinitionFixture"),
												   {{Key, R.Revision, 1, Out.Payload, false}}});
					Out.OK = Result.Status == ECommitStatus::Committed;
					Out.Revision = R.Revision + 1;
				}
			}
			DB.Close();
			return Out;
		});
	}
	bool Check(bool OK, const TCHAR *Message)
	{
		return Test->TestTrue(Message, OK);
	}
	bool RejectHasNoPartialState()
	{
		return Check(Done && !Success && !Profiles->IsReady(Current.Get()) &&
						 Current->FindComponentByClass<UProject_JProgressionComponent>()->GetState().Revision == 0 &&
						 Current->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 0 &&
						 Current->GetInventoryComponent()->GetItemInstances().IsEmpty() &&
						 Current->GetEquipmentManagerComponent()->GetPrivateEquipmentCount() == 0,
					 TEXT("Unknown item/version rejects before any authoritative state or equipment is installed"));
	}

  public:
	FProfileFailureCases(FAutomationTestBase *T, UGameInstance *Instance, FString Database)
		: Test(T), GI(Instance), World(Instance->GetWorld()),
		  Profiles(Instance->GetSubsystem<UProject_JLocalProfileSubsystem>()), File(MoveTemp(Database))
	{
	}
	~FProfileFailureCases()
	{
		Profiles->HoldCommandCompletionsForTest(false);
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30)
		{
			Test->AddError(TEXT("Failure/lifetime fixture timed out"));
			return true;
		}
		if (Step == 0)
		{
			Open();
			Old = Current;
			Step = 1;
			return false;
		}
		if (Step == 1)
		{
			if (!Done)
				return false;
			if (!Check(Success, TEXT("Owned profile starts normally")))
				return true;
			Old->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Old.Get(), nullptr);
			Old->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			Step = 2;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Old.Get()))
				return false;
			Profiles->HoldCommandCompletionsForTest(true);
			Check(Old->FindComponentByClass<UProject_JProgressionComponent>()->GrantExperience(9),
				  TEXT("Actual native command admitted"));
			Step = 3;
			return false;
		}
		if (Step == 3)
		{
			if (!Profiles->GetHeldCompletionCountForTest())
				return false;
			Check(Old->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 0,
				  TEXT("Committed native write is not prematurely visible before completion"));
			Profiles->Detach(Old.Get());
			Open();
			Step = 4;
			return false;
		}
		if (Step == 4)
		{
			if (!Done)
				return false;
			if (!Check(Success && Current->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 9,
					   TEXT("New binding restores the actual committed result")))
				return true;
			Profiles->HoldCommandCompletionsForTest(false);
			Check(Old->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 0 &&
					  Current->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 9 &&
					  !Old->FindComponentByClass<UProject_JProgressionComponent>()->GrantExperience(1),
				  TEXT("Old completion arriving after new binding neither publishes nor revives its writer"));
			Profiles->FailCommandWritesForTest(Current.Get());
			Check(Current->FindComponentByClass<UProject_JProgressionComponent>()->GrantExperience(2),
				  TEXT("Write-failure fixture enters the actual adapter"));
			Step = 5;
			return false;
		}
		if (Step == 5)
		{
			if (Profiles->IsCheckpointPending(Current.Get()))
				return false;
			Check(!Profiles->IsReady(Current.Get()) &&
					  Current->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 9 &&
					  Profiles->GetStatusText(Current.Get()).ToString().Contains(TEXT("실패")),
				  TEXT("Native I/O failure is visible and cannot publish an uncommitted reward"));
			CorruptDefinition(TEXT("Unknown.Definition.IsolatedFixture"), 1);
			Step = 6;
			return false;
		}
		if (Step == 6)
		{
			if (!Corrupt.IsReady())
				return false;
			Expected = Corrupt.Get();
			if (!Check(Expected.OK, TEXT("Native failed write preserved last confirmed data; isolated "
										 "unknown-definition fixture committed")))
				return true;
			Open();
			Step = 7;
			return false;
		}
		if (Step == 7)
		{
			if (!Done)
				return false;
			if (!RejectHasNoPartialState())
				return true;
			const auto *Potion = LoadObject<UProject_JConsumableDefinition>(
				nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
			if (!Check(Potion != nullptr, TEXT("Actual item supplies version-mismatch fixture")))
				return true;
			CorruptDefinition(Potion->ItemId, Potion->SchemaVersion + 1);
			Step = 8;
			return false;
		}
		if (Step == 8)
		{
			if (!Corrupt.IsReady())
				return false;
			Expected = Corrupt.Get();
			if (!Check(Expected.OK, TEXT("Native isolated version-mismatch fixture committed")))
				return true;
			Open();
			Step = 9;
			return false;
		}
		if (Step == 9)
		{
			if (!Done)
				return false;
			RejectHasNoPartialState();
			const FGuid Character = Current->GetCharacterId().Value;
			Profiles->Detach(Current.Get());
			Preserved = Async(EAsyncExecution::ThreadPool, [File = File, Character, Expected = Expected] {
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				const bool OK = DB.Read({TEXT("LocalProfile"), Character}, R) && R.Revision == Expected.Revision &&
								R.Payload == Expected.Payload;
				DB.Close();
				return OK;
			});
			Step = 10;
			return false;
		}
		if (!Preserved.IsReady())
			return false;
		Check(Preserved.Get(), TEXT("Rejected restore never resets or overwrites the incompatible saved profile"));
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocalProfileFailureTest, "ProjectJ.MMO.Persistence.FailureAndLateCompletion",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocalProfileFailureTest::RunTest(const FString &)
{
	AddExpectedError(TEXT("Character data registry is empty."), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Local profile load rejected;"), EAutomationExpectedErrorFlags::Contains, 2);
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	auto *Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	const FString File =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/LocalProfileFailures") /
										  FGuid::NewGuid().ToString() / TEXT("Profiles.sqlite"));
	Profiles->DatabasePath = File;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileFailureCases(this, GI, File));
	return true;
}
#endif
