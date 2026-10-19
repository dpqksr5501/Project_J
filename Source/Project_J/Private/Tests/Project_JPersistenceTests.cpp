#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UObject/StrongObjectPtr.h"

using namespace ProjectJ::MMO;
namespace
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FString TestDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/LocalProfileTests") /
											 FGuid::NewGuid().ToString());
}
struct FChecks
{
	TArray<FString> Failures;
	void Check(bool Value, const TCHAR *Message)
	{
		if (!Value)
			Failures.Add(Message);
	}
};
class FRepositoryFinished : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TFuture<FChecks> Work;

  public:
	FRepositoryFinished(FAutomationTestBase *InTest, TFuture<FChecks> &&InWork) : Test(InTest), Work(MoveTemp(InWork))
	{
	}
	bool Update() override
	{
		if (!Work.IsReady())
			return false;
		for (const auto &Failure : Work.Get().Failures)
			Test->AddError(Failure);
		return true;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSQLiteContractTest, "ProjectJ.MMO.Persistence.SQLiteContract", Flags)
bool FProjectJSQLiteContractTest::RunTest(const FString &)
{
	AddExpectedError(TEXT("Local database has a journal/WAL sidecar."), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Directory = TestDirectory();
	ADD_LATENT_AUTOMATION_COMMAND(FRepositoryFinished(
		this, Async(EAsyncExecution::ThreadPool, [Directory] {
			FChecks C;
			const FString File = Directory / TEXT("Repository.sqlite");
			const FAggregateKey A{TEXT("Character"), FGuid::NewGuid()}, B{TEXT("Quest"), FGuid::NewGuid()};
			FCommitRequest Initial{FGuid::NewGuid(), TEXT("Reward"), {{A, 0, 1, {1, 2}, false}, {B, 0, 1, {3}, false}}};
			{
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Missing,
						TEXT("New database distinguishes absent from unavailable"));
				C.Check(DB.Commit(Initial).Status == ECommitStatus::Committed,
						TEXT("State and receipt atomically commit"));
				DB.Close();
			}
			{
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				const auto Replay = DB.Commit(Initial);
				C.Check(Replay.Status == ECommitStatus::Replayed && Replay.Revisions == TArray<int64>{1, 1},
						TEXT("Reopened database returns original durable receipt"));
				auto Changed = Initial;
				Changed.Mutations[0].Payload = {9};
				C.Check(DB.Commit(Changed).Status == ECommitStatus::KeyReused,
						TEXT("A receipt cannot be reused for a changed payload"));
				FCommitRequest Conflict{
					FGuid::NewGuid(), TEXT("Conflict"), {{A, 1, 1, {8}, false}, {B, 0, 1, {8}, false}}};
				C.Check(DB.Commit(Conflict).Status == ECommitStatus::Conflict,
						TEXT("One stale revision rejects the full transaction"));
				C.Check(DB.Read(A, R) && R.Revision == 1 && R.Payload == TArray<uint8>{1, 2},
						TEXT("Conflict leaves earlier mutations untouched"));
				FCommitRequest Delete{FGuid::NewGuid(), TEXT("Delete"), {{A, 1, 1, {}, true}}};
				C.Check(DB.Commit(Delete).Status == ECommitStatus::Committed && DB.Read(A, R) && R.bDeleted &&
							R.Revision == 2 && R.Payload.IsEmpty(),
						TEXT("Tombstone retains its revision with an empty blob"));
				FCommitRequest Recreate{FGuid::NewGuid(), TEXT("Recreate"), {{A, 0, 1, {4}, false}}};
				C.Check(DB.Commit(Recreate).Status == ECommitStatus::Conflict,
						TEXT("Delete does not permit an ABA recreation at revision zero"));
				Recreate.Mutations[0].ExpectedRevision = 2;
				C.Check(DB.Commit(Recreate).Status == ECommitStatus::Committed,
						TEXT("Explicit tombstone revision permits recreation"));
				const FString Backup = Directory / TEXT("Backup.sqlite");
				C.Check(DB.BackupToNewFile(Backup), TEXT("Consistent backup passes native integrity validation"));
				C.Check(!DB.BackupToNewFile(Backup), TEXT("Backup never overwrites an existing file"));
				DB.Close();
				const FString Restored = Directory / TEXT("Restored.sqlite");
				C.Check(IFileManager::Get().Copy(*Restored, *Backup, false) == COPY_OK,
						TEXT("Restore fixture copies a verified closed backup"));
				FProject_JSQLiteRepository Recovery(Restored);
				C.Check(Recovery.Read(A, R) && R.Revision == 3 && R.Payload == TArray<uint8>{4},
						TEXT("Restored backup retains latest data"));
				C.Check(Recovery.Commit(Initial).Status == ECommitStatus::Replayed,
						TEXT("Restored backup retains deduplication receipts"));
				Recovery.Close();
			}
			{
				// Independent adapters race one expected revision. The OS file gate is
				// required by unreal-fs; the DB transaction still rejects the stale writer.
				auto Writer = [File, A](uint8 Value) {
					FProject_JSQLiteRepository DB(File);
					const auto Status =
						DB.Commit({FGuid::NewGuid(), TEXT("Concurrent"), {{A, 3, 1, {Value}, false}}}).Status;
					DB.Close();
					return Status;
				};
				auto First = Async(EAsyncExecution::Thread, [Writer] { return Writer(5); });
				auto Second = Async(EAsyncExecution::Thread, [Writer] { return Writer(6); });
				const auto One = First.Get(), Two = Second.Get();
				C.Check((One == ECommitStatus::Committed && Two == ECommitStatus::Conflict) ||
							(Two == ECommitStatus::Committed && One == ECommitStatus::Conflict),
						TEXT("Independent SQLite writers admit exactly one expected revision"));
			}
			{
				TArray<uint8> Before, After;
				FProject_JSQLiteRepository DB(File);
				const FGuid Id = FGuid::NewGuid();
				const FAggregateKey Lease{TEXT("LocalProfileLease"), Id}, Profile{TEXT("LocalProfile"), Id};
				TArray<uint8> One, Two;
				One.Init(1, 16);
				Two.Init(2, 16);
				C.Check(DB.Commit({FGuid::NewGuid(),
								   TEXT("OldSession"),
								   {{Lease, 0, 1, One, false}, {Profile, 0, 1, {17}, false}}})
								.Status == ECommitStatus::Committed,
						TEXT("Owned profile and initial writer epoch commit together"));
				C.Check(DB.Commit({FGuid::NewGuid(), TEXT("NewSession"), {{Lease, 1, 1, Two, false}}}).Status ==
							ECommitStatus::Committed,
						TEXT("A new session advances the durable writer epoch"));
				C.Check(DB.Commit({FGuid::NewGuid(),
								   TEXT("LateOldWriter"),
								   {{Lease, 1, 1, One, false}, {Profile, 1, 1, {99}, false}}})
								.Status == ECommitStatus::Conflict,
						TEXT("Stale writer epoch rejects the whole state/receipt commit"));
				FRecord Current;
				C.Check(DB.Read(Profile, Current) && Current.Revision == 1 && Current.Payload == TArray<uint8>{17},
						TEXT("Fenced late writer cannot partly overwrite the admitted profile"));
				DB.Close();
				FFileHelper::LoadFileToArray(Before, *File);
				C.Check(FFileHelper::SaveArrayToFile(TArray<uint8>{1, 2, 3}, *(File + TEXT("-journal"))),
						TEXT("Interrupted-file fixture exists"));
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("A leftover journal blocks admission before inspecting uncommitted pages"));
				DB.Close();
				FFileHelper::LoadFileToArray(After, *File);
				C.Check(Before == After && IFileManager::Get().FileSize(*(File + TEXT("-journal"))) == 3,
						TEXT("Rejected journal and database remain untouched for offline recovery"));
			}
			{
				FProject_JSQLiteRepository Limited(Directory / TEXT("Capacity.sqlite"), 2, 1);
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Committed,
						TEXT("Bounded repository admits its final receipt"));
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Replayed,
						TEXT("Capacity does not evict an original receipt"));
				Initial.IdempotencyKey = FGuid::NewGuid();
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Capacity, TEXT("Capacity fails closed"));
				Limited.Close();
			}
			{
				FSQLiteDatabase Future;
				const FString Unknown = Directory / TEXT("Unknown.sqlite");
				C.Check(Future.Open(*Unknown, ESQLiteDatabaseOpenMode::ReadWriteCreate) &&
							Future.SetApplicationId(0x504A5250) && Future.SetUserVersion(99),
						TEXT("Unknown schema fixture exists"));
				Future.Close();
				FProject_JSQLiteRepository DB(Unknown);
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Unavailable &&
							DB.Commit(Initial).Status == ECommitStatus::Unavailable,
						TEXT("Unknown schema cannot be silently reset or written"));
				DB.Close();
				C.Check(Future.Open(*Unknown, ESQLiteDatabaseOpenMode::ReadOnly),
						TEXT("Unknown file remains readable"));
				int32 Version = 0;
				C.Check(Future.GetUserVersion(Version) && Version == 99, TEXT("Rejected database keeps its version"));
				Future.Close();
				const FString Corrupt = Directory / TEXT("Corrupt.sqlite");
				FFileHelper::SaveStringToFile(TEXT("not a database"), *Corrupt);
				FProject_JSQLiteRepository Broken(Corrupt);
				C.Check(Broken.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("Corruption is unavailable, never an empty new profile"));
				Broken.Close();
				FString Text;
				C.Check(FFileHelper::LoadFileToString(Text, *Corrupt) && Text == TEXT("not a database"),
						TEXT("Corrupt file is preserved"));
			}
			{
				const FString IntentFile = Directory / TEXT("Intent.sqlite");
				FCommitRequest Command{FGuid::NewGuid(), TEXT("ProfileEdit.1"), {{A, 0, 1, {4}, false}}};
				FProject_JSQLiteRepository DB(IntentFile);
				const TArray<uint8> Intent{1, 2, 3};
				C.Check(DB.CommitWithIntent(Command, Intent).Status == ECommitStatus::Committed,
						TEXT("Opaque intent and state commit atomically"));
				DB.Close();
				C.Check(DB.FindReceipt(Command.IdempotencyKey, Command.Operation, Intent).Status ==
							ECommitStatus::Replayed,
						TEXT("Exact intent reconciles after reopening independent of aggregate revisions"));
				C.Check(DB.FindReceipt(Command.IdempotencyKey, Command.Operation, {1, 2, 4}).Status ==
							ECommitStatus::KeyReused,
						TEXT("Changed intent cannot reuse a logical request"));
				C.Check(DB.Commit(Command).Status == ECommitStatus::KeyReused,
						TEXT("Generic retry cannot silently drop an existing intent"));
				DB.Close();
				FProject_JSQLiteRepository Missing(Directory / TEXT("Absent.sqlite"));
				C.Check(!Missing.BackupToNewFile(Directory / TEXT("AbsentBackup.sqlite")) &&
							!IFileManager::Get().FileExists(*(Directory / TEXT("Absent.sqlite"))),
						TEXT("Backup does not create an absent source database"));
				FSQLiteDatabase Foreign;
				const FString ForeignFile = Directory / TEXT("Foreign.sqlite");
				C.Check(Foreign.Open(*ForeignFile, ESQLiteDatabaseOpenMode::ReadWriteCreate) &&
							Foreign.Execute(TEXT("CREATE VIEW foreign_view AS SELECT 1")),
						TEXT("Unmarked view-only foreign schema fixture exists"));
				Foreign.Close();
				FProject_JSQLiteRepository Unknown(ForeignFile);
				FRecord R;
				C.Check(Unknown.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("A view-only foreign database is not adopted as an empty profile"));
				Unknown.Close();
			}
			return C;
		})));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileCodecTest, "ProjectJ.MMO.Persistence.ProfileCodec", Flags)
bool FProjectJProfileCodecTest::RunTest(const FString &)
{
	FGuid A, C, A2, C2;
	TestTrue(TEXT("Local profile names resolve"),
			 UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("Developer_1"), A, C));
	TestTrue(TEXT("Names normalize case"),
			 UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("developer_1"), A2, C2) && A == A2 && C == C2 &&
				 A != C);
	TestFalse(TEXT("Path traversal cannot be a profile"),
			  UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("../profile"), A2, C2));
	FProject_JLocalProfileSnapshot Snapshot;
	Snapshot.Account = A;
	Snapshot.Character = C;
	Snapshot.RulesFingerprint = FString::ChrN(40, 'A');
	Snapshot.Progression.Experience = MAX_int64;
	Snapshot.Quests.States.Add({TEXT("Quest"), 2, true, false});
	TArray<uint8> Bytes;
	FProject_JLocalProfileSnapshot Restored;
	TestTrue(TEXT("Values encode"), Snapshot.Encode(Bytes));
	TestTrue(TEXT("64-bit XP survives without JSON rounding"),
			 FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) && Restored.Progression.Experience == MAX_int64);
	TestTrue(TEXT("Quest state stays paired with progression"), Restored.Quests.States.Num() == 1 &&
																	Restored.Quests.States[0].bClaimed &&
																	!Restored.Quests.States[0].bTracked);
	const auto Duplicate = Snapshot.Quests.States[0];
	Snapshot.Quests.States.Add(Duplicate);
	TestFalse(TEXT("Writer cannot commit an unreadable duplicate-ID snapshot"), Snapshot.Encode(Bytes));
	TestFalse(TEXT("Duplicate quest IDs cannot restore"), FProject_JLocalProfileSnapshot::Decode(Bytes, Restored));
	Snapshot.Quests.States.Pop();
	FProject_JPersistentItem Item;
	Item.Instance = FGuid::NewGuid();
	Item.Definition = TEXT("Potion.Test");
	Item.Order = 0;
	Snapshot.Items.Add(Item);
	TestTrue(TEXT("Stable item values encode"), Snapshot.Encode(Bytes));
	TestTrue(TEXT("Stable item values round trip"), FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) &&
														Restored.Items.Num() == 1 &&
														Restored.Items[0].Instance == Item.Instance);
	Snapshot.Items.Add(Item);
	TestFalse(TEXT("Duplicate instance rejects before writing"), Snapshot.Encode(Bytes));
	Snapshot.Items[1].Instance = FGuid::NewGuid();
	TestFalse(TEXT("Duplicate bag order rejects before writing"), Snapshot.Encode(Bytes));
	Snapshot.Items.Pop();
	Snapshot.Items[0].DefinitionVersion = 0;
	TestFalse(TEXT("Unknown definition version cannot encode"), Snapshot.Encode(Bytes));
	Snapshot.Items[0].DefinitionVersion = 1;
	Snapshot.Format = 1;
	TestFalse(TEXT("Legacy format cannot silently discard owned items"), Snapshot.Encode(Bytes));
	Bytes.Init('x', 65537);
	TestFalse(TEXT("Oversized documents reject before parsing"),
			  FProject_JLocalProfileSnapshot::Decode(Bytes, Restored));
	return true;
}

namespace
{
struct FProfileLifecycle : IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World = nullptr;
	UProject_JLocalProfileSubsystem *Profiles = nullptr;
	TWeakObjectPtr<AProject_JPlayerState> Player;
	FString Phase, Database;
	int32 Step = 0;
	int32 SmallGrants = 0;
	FGuid Potion, Gear, UseRequest, Split;
	float HealedHealth = 0;
	bool bItemsVerified = false;
	bool bOpenDone = false, bOpenSuccess = false, bBackupDone = false, bBackupSuccess = false;
	bool bShutdown = false;
	TFuture<bool> ShutdownRead;
	FString Backup;
	double Started = FPlatformTime::Seconds();
	FProfileLifecycle(FAutomationTestBase *InTest, UGameInstance *InGI, FString InPhase, FString InDatabase)
		: Test(InTest), GI(InGI), World(InGI->GetWorld()),
		  Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()), Phase(MoveTemp(InPhase)),
		  Database(MoveTemp(InDatabase))
	{
	}
	~FProfileLifecycle()
	{
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		if (!bShutdown)
			GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void Open()
	{
		auto *State = World->SpawnActor<AProject_JPlayerState>();
		Player = State;
		auto *Q = State->FindComponentByClass<UProject_JQuestComponent>();
		auto *Definition = NewObject<UProject_JQuestDefinition>(State);
		Definition->QuestId = TEXT("PersistenceQuest");
		Definition->ObjectiveEvent = TEXT("PersistenceEvent");
		Definition->RequiredCount = 2;
		Definition->RewardExperience = 17;
		Q->Definitions.Add(Definition);
		bOpenDone = bOpenSuccess = false;
		Profiles->OpenProfile(State, TEXT("AutomationProfile"), [this](bool Success) {
			bOpenDone = true;
			bOpenSuccess = Success;
		});
	}
	bool Check(bool Condition, const TCHAR *Message)
	{
		return Test->TestTrue(Message, Condition);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30)
		{
			Test->AddError(TEXT("Local profile lifecycle timed out"));
			return true;
		}
		if (Step == 4)
		{
			if (!ShutdownRead.IsReady())
				return false;
			Check(ShutdownRead.Get(), TEXT("Normal GameInstance shutdown drains the newest accepted checkpoint"));
			return true;
		}
		if (Step == 0)
		{
			Open();
			Step = 1;
			return false;
		}
		if (Step == 1)
		{
			if (!bOpenDone)
				return false;
			if (!Check(bOpenSuccess && Profiles->IsReady(Player.Get()),
					   TEXT("Actual subsystem finishes restoring before gameplay admission")))
				return true;
			auto *Duplicate = World->SpawnActor<AProject_JPlayerState>();
			bool DuplicateAccepted = true;
			Profiles->OpenProfile(Duplicate, TEXT("AutomationProfile"),
								  [&DuplicateAccepted](bool Success) { DuplicateAccepted = Success; });
			Check(!DuplicateAccepted && Profiles->IsReady(Player.Get()),
				  TEXT("Duplicate local binding is rejected without retiring the admitted player"));
			Duplicate->Destroy();
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			if (Phase == TEXT("Read") || Phase == TEXT("Reopen"))
			{
				const auto Items = Player->GetInventoryComponent()->GetItemInstances();
				FProject_JItemInstanceData Equipped;
				Check(Items.Num() == 2 && Items.ContainsByPredicate([](const auto &V) {
					return Cast<UProject_JConsumableDefinition>(V.ItemDef) && V.StackCount == 2;
				}) &&
						  Player->GetEquipmentManagerComponent()->GetEquippedItemInstance(
							  EProject_JEquipmentSlot::Weapon, Equipped) &&
						  Equipped.bIsEquipped && Equipped.bIsLocked &&
						  Items.ContainsByPredicate([&](const auto &V) { return V.InstanceId == Equipped.InstanceId; }),
					  TEXT("Consumed inventory, stable instance and equipped ownership restore together"));
				Check(P->GetExperience() == 74 && P->GetState().Level == 1,
					  TEXT("New session restores the last committed XP"));
				Check(Q->Claim(TEXT("PersistenceQuest")) && !Profiles->IsCheckpointPending(Player.Get()) &&
						  P->GetExperience() == 74,
					  TEXT("Restored claim reports durable claimed state without a new receipt or reward"));
				Check(Q->States.ContainsByPredicate([](const auto &S) {
					return S.QuestId == TEXT("PersistenceQuest") && S.Count == 2 && S.bClaimed;
				}),
					  TEXT("XP and claimed state restore together"));
				Step = 20;
				return false;
			}
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
			P->InitializeDefaults(nullptr, nullptr, 1);
			Step = 10;
			return false;
		}
		if (Step == 20)
		{
			if (Profiles->IsCheckpointPending(Player.Get()))
				return false;
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			Check(Profiles->IsReady(Player.Get()) && P->GetExperience() == 74,
				  TEXT("Durable claim replay restores state without duplicate XP"));
			if (Phase == TEXT("Reopen"))
			{
				const FGuid Id = Player->GetCharacterId().Value;
				Check(P->GrantExperience(1) && Profiles->IsCheckpointPending(Player.Get()),
					  TEXT("A final native write is outstanding before normal shutdown"));
				World->EndPlay(EEndPlayReason::Quit);
				GI->Shutdown();
				bShutdown = true;
				ShutdownRead = Async(EAsyncExecution::ThreadPool, [File = Database, Id]() {
					FProject_JSQLiteRepository Repository(File);
					FRecord Record;
					FProject_JLocalProfileSnapshot Snapshot;
					const bool Success = Repository.Read({TEXT("LocalProfile"), Id}, Record) &&
										 FProject_JLocalProfileSnapshot::Decode(Record.Payload, Snapshot) &&
										 Snapshot.Progression.Experience == 75;
					Repository.Close();
					return Success;
				});
				Step = 4;
				return false;
			}
			// Identity retirement must prevent later callbacks from writing to the old profile.
			const auto Character = Player->GetCharacterId();
			Player->SetIdentity(Player->GetAccountId(), FProject_JCharacterId::NewId());
			Check(!Profiles->IsReady(Player.Get()), TEXT("Identity replacement immediately revokes the local binding"));
			P->GrantExperience(1);
			Profiles->RequestCheckpoint(Player.Get());
			Check(!Profiles->IsCheckpointPending(Player.Get()), TEXT("Retired identity cannot enqueue another write"));
			return true;
		}
		if (Step >= 10 && Step <= 14)
		{
			if (Profiles->IsCheckpointPending(Player.Get()))
				return false;
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			if (Step == 10)
			{
				Check(P->GrantExperience(50) && P->GetExperience() == 0,
					  TEXT("Existing XP entry admits a native commit before publication"));
				Check(!P->GrantExperience(1), TEXT("A competing command is rejected while one commit is pending"));
				Step = 11;
				return false;
			}
			if (Step == 11)
			{
				Check(P->GetExperience() == 50, TEXT("Confirmed native XP command publishes once"));
				Check(Q->Accept(TEXT("PersistenceQuest")), TEXT("Existing quest writer accepts"));
				Q->RecordObjective(TEXT("PersistenceEvent"), 2);
				Step = 12;
				return false;
			}
			if (Step == 12)
			{
				Check(Q->Claim(TEXT("PersistenceQuest")) && P->GetExperience() == 50,
					  TEXT("Claim and XP remain unchanged before durable confirmation"));
				Step = 13;
				return false;
			}
			if (Step == 13)
			{
				Check(P->GetExperience() == 67 && Q->States.ContainsByPredicate([](const auto &S) {
					return S.QuestId == TEXT("PersistenceQuest") && S.bClaimed;
				}),
					  TEXT("Claim and XP publish together after atomic commit"));
				Step = 14;
			}
			if (SmallGrants < 7)
			{
				Check(P->GrantExperience(1) && P->GetExperience() == 67 + SmallGrants,
					  TEXT("Each accepted trusted grant waits for disk before publication"));
				++SmallGrants;
				return false;
			}
			Check(P->GetExperience() == 74, TEXT("All confirmed grants retained"));
			Step = 2;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player.Get()))
				return false;
			if (!Check(Profiles->IsReady(Player.Get()) && Profiles->GetCommittedRevision(Player.Get()) > 0,
					   TEXT("Save is acknowledged only after native commit")))
				return true;
			if (!bItemsVerified)
			{
				auto *Def = LoadObject<UProject_JConsumableDefinition>(
					nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
				Check(Def && Def->RestoreHealth > 0, TEXT("Actual authored consumable resolves"));
				Check(!Player->GetInventoryComponent()->AddItemDefinition(Def, 3).IsValid() &&
						  Player->GetInventoryComponent()->GetItemInstances().IsEmpty() &&
						  Profiles->IsCheckpointPending(Player.Get()),
					  TEXT("Existing AddItem entry publishes ownership only after disk commit"));
				Step = 30;
				return false;
			}
			Profiles->RequestBackup([this](bool Success, FString Path) {
				bBackupDone = true;
				bBackupSuccess = Success;
				Backup = MoveTemp(Path);
			});
			Step = 3;
			return false;
		}
		if (Step >= 30 && Step <= 40)
		{
			if (Step != 38 && Profiles->IsCheckpointPending(Player.Get()))
				return false;
			if (!Check(Profiles->IsReady(Player.Get()), TEXT("Persistent item command remains ready")))
				return true;
			auto *I = Player->GetInventoryComponent();
			auto *E = Player->GetEquipmentManagerComponent();
			auto *ASC = Player->GetProjectJAbilitySystemComponent();
			const auto Items = I->GetItemInstances();
			FProject_JItemInstanceData Item;
			if (Step == 30)
			{
				if (!Check(Items.Num() == 1 && Items[0].StackCount == 3,
						   TEXT("Confirmed AddItem publishes exact authored stack")))
					return true;
				Potion = Items[0].InstanceId;
				auto *Def = LoadObject<UProject_JEquipmentItemDefinition>(
					nullptr,
					TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
				Check(Def && Def->EquipmentSlot == EProject_JEquipmentSlot::Weapon,
					  TEXT("Actual authored equipment resolves"));
				I->AddItemDefinition(Def);
				Step = 31;
				return false;
			}
			if (Step == 31)
			{
				const auto *FoundGear = Items.FindByPredicate(
					[](const auto &V) { return Cast<UProject_JEquipmentItemDefinition>(V.ItemDef); });
				if (!Check(FoundGear != nullptr, TEXT("Equipment ownership is committed")))
					return true;
				Gear = FoundGear->InstanceId;
				E->RequestEquipmentChange(E->NewRequestId(), Gear, EProject_JEquipmentSlot::Weapon, false);
				Check(!E->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon),
					  TEXT("Equip request does not publish before commit"));
				Step = 32;
				return false;
			}
			if (Step == 32)
			{
				Check(E->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Item) && Item.InstanceId == Gear &&
						  I->FindItemInstance(Gear, Item) && Item.bIsEquipped && Item.bIsLocked,
					  TEXT("Slot and inventory lock publish coherently"));
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetManaAttribute(), 100);
				UseRequest = I->NewRequestId();
				Profiles->LoseNextCommitAcknowledgementForTest(Player.Get());
				I->RequestUseItem(UseRequest, Potion);
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 3 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Consumption and healing wait for durable acknowledgement"));
				Step = 33;
				return false;
			}
			if (Step == 33)
			{
				HealedHealth = ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 && HealedHealth > 40,
					  TEXT("Lost acknowledgement reconciles same key and applies consumption/heal once"));
				I->RequestUseItem(UseRequest, Potion);
				Step = 34;
				return false;
			}
			if (Step == 34)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == HealedHealth,
					  TEXT("Same use request replays without consuming/healing twice"));
				I->RequestUseItem(UseRequest, Gear);
				Step = 35;
				return false;
			}
			if (Step == 35)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  E->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon),
					  TEXT("Same request with a different item is rejected without mutation"));
				FProject_JBagRequest R;
				R.Source = Potion;
				R.Count = 1;
				R.ExpectedSourceCount = 2;
				R.ExpectedSourceOrder = Item.BagOrder;
				I->RequestBagChange(I->NewRequestId(), R);
				Step = 36;
				return false;
			}
			if (Step == 36)
			{
				Check(Items.Num() == 3 && I->FindItemInstance(Potion, Item) && Item.StackCount == 1,
					  TEXT("Persistent split preserves both stacks"));
				const auto *New = Items.FindByPredicate(
					[&](const auto &V) { return V.InstanceId != Potion && V.InstanceId != Gear; });
				if (!Check(New != nullptr, TEXT("Split creates one stable instance")))
					return true;
				Split = New->InstanceId;
				FProject_JBagRequest R;
				R.Operation = EProject_JBagOperation::Merge;
				R.Source = Split;
				R.Target = Potion;
				R.Count = 1;
				R.ExpectedSourceCount = 1;
				R.ExpectedTargetCount = 1;
				R.ExpectedSourceOrder = New->BagOrder;
				R.ExpectedTargetOrder = Item.BagOrder;
				I->RequestBagChange(I->NewRequestId(), R);
				Step = 37;
				return false;
			}
			if (Step == 37)
			{
				Check(Items.Num() == 2 && I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  !I->HasItemInstance(Split),
					  TEXT("Persistent merge consumes the source exactly once"));
				// UWorld clamps a large delta. Advance the real test world in bounded
				// ticks until the authored cooldown actually expires; never reset it.
				World->Tick(LEVELTICK_All, .1f);
				if (I->GetUseCooldownRemaining() > 0)
					return false;
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40);
				UseRequest = I->NewRequestId();
				Profiles->HoldCommandCompletionsForTest(true);
				I->RequestUseItem(UseRequest, Potion);
				Step = 38;
				return false;
			}
			if (Step == 38)
			{
				if (!Profiles->GetHeldCompletionCountForTest())
					return false;
				const uint64 Previous = ASC->GetActorInfoRevision();
				ASC->ClearActorInfo();
				ASC->InitAbilityActorInfo(Player.Get(), nullptr);
				Profiles->HoldCommandCompletionsForTest(false);
				Test->TestEqual(TEXT("Retired recipient health stays unchanged"),
								ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 40.f);
				Test->TestEqual(TEXT("Retired recipient cost remains committed"),
								I->FindItemInstance(Potion, Item) ? Item.StackCount : INDEX_NONE, 1);
				Check(ASC->GetActorInfoRevision() != Previous && I->FindItemInstance(Potion, Item) &&
						  Item.StackCount == 1 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Late consumption completion cannot heal a replaced actor-info binding even when "
						   "owner/avatar pointers match"));
				I->RequestUseItem(UseRequest, Potion);
				Step = 39;
				return false;
			}
			if (Step == 39)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 1 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Reconciliation of a retired recipient never applies its recovery twice"));
				Check(I->SetItemStackCount(Potion, 2) && I->FindItemInstance(Potion, Item) && Item.StackCount == 1,
					  TEXT("Trusted replenishment uses the same durable boundary"));
				Step = 40;
				return false;
			}
			Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2,
				  TEXT("Replenishment is confirmed before restart"));
			bItemsVerified = true;
			Step = 2;
			return false;
		}
		if (!bBackupDone)
			return false;
		Check(bBackupSuccess && IFileManager::Get().FileExists(*Backup),
			  TEXT("Actual backup consumer produces a verified new file"));
		Profiles->Detach(Player.Get());
		Player->Destroy();
		if (Phase == TEXT("Write"))
			return true;
		Phase = TEXT("Reopen");
		Open();
		Step = 1;
		return false;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocalProfileLifecycleTest, "ProjectJ.MMO.Persistence.LocalProfileLifecycle",
								 Flags)
bool FProjectJLocalProfileLifecycleTest::RunTest(const FString &)
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJProfilePhase="), Phase);
	if (!Phase.IsEmpty() && Phase != TEXT("Write") && Phase != TEXT("Read"))
	{
		AddError(TEXT("Unknown profile fixture phase"));
		return false;
	}
	FString Directory = TestDirectory();
	if (!Phase.IsEmpty())
		Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
													  TEXT("Validation/LocalProfileProcessRestart"));
	const FString Database = Directory / TEXT("Profiles.sqlite");
	if (Phase == TEXT("Write") && IFileManager::Get().FileExists(*Database))
	{
		AddError(TEXT("Preserve existing process-restart fixture; use a fresh validation run directory"));
		return false;
	}
	AddExpectedError(TEXT("Character data registry is empty."), EAutomationExpectedErrorFlags::Contains, 1);
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	auto *Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("Real GameInstance owns the persistence adapter"), Profiles))
		return false;
	Profiles->DatabasePath = Database;
	auto *Definitions = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	TestTrue(TEXT("Existing registry explicitly loads authored PrimaryAssets"),
			 Definitions && Definitions->LoadPrimaryAssetDefinitions());
	auto *AuthoredClass = LoadObject<UProject_JCharacterClassDefinition>(
		nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
	TestTrue(TEXT("Actual default character class resolves through the same saved-ID registry"),
			 AuthoredClass && Definitions->GetClassDefinition(AuthoredClass->ClassId) == AuthoredClass);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileLifecycle(this, GI, Phase, Database));
	return true;
}
#endif
