#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JProfileMigration.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"

using namespace ProjectJ::MMO;
namespace
{
class FMigrationCases : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World;
	UProject_JLocalProfileSubsystem *Profiles;
	AProject_JPlayerState *Player = nullptr;
	FString File, Directory;
	int32 Step = 0;
	bool Ready = false, Success = false;
	TFuture<FProject_JLocalProfileSnapshot> Read;
	TFuture<TArray<FString>> Write;
	double Start = FPlatformTime::Seconds();

  public:
	FMigrationCases(FAutomationTestBase *T, UGameInstance *G, FString F)
		: Test(T), GI(G), World(G->GetWorld()), Profiles(G->GetSubsystem<UProject_JLocalProfileSubsystem>()), File(F),
		  Directory(FPaths::GetPath(F))
	{
	}
	~FMigrationCases()
	{
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 60)
		{
			Test->AddError(TEXT("Migration fixture timed out"));
			return true;
		}
		if (Step == 0)
		{
			Player = World->SpawnActor<AProject_JPlayerState>();
			Profiles->OpenProfile(Player, TEXT("MigrationFixture"), [this](bool OK) {
				Ready = true;
				Success = OK;
			});
			++Step;
			return false;
		}
		if (Step == 1)
		{
			if (!Ready)
				return false;
			if (!Test->TestTrue(TEXT("Native profile starts before migration fixture generation"), Success))
				return true;
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			++Step;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			Read = Async(EAsyncExecution::ThreadPool, [File = File, Id = Player->GetCharacterId().Value] {
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				FProject_JLocalProfileSnapshot S;
				if (DB.Read({TEXT("LocalProfile"), Id}, R))
					FProject_JLocalProfileSnapshot::Decode(R.Payload, S);
				DB.Close();
				return S;
			});
			++Step;
			return false;
		}
		if (Step == 3)
		{
			if (!Read.IsReady())
				return false;
			auto Owned = Read.Get();
			auto *Potion = LoadObject<UProject_JConsumableDefinition>(
				nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
			if (!Test->TestTrue(TEXT("Actual saved rules and authored consumable are available"),
								Owned.Account.IsValid() && Potion))
				return true;
			FProject_JPersistentItem Item;
			Item.Instance = FGuid::NewGuid();
			Item.Definition = Potion->ItemId;
			Item.DefinitionVersion = Potion->SchemaVersion;
			Item.Count = 2;
			Item.Order = 0;
			Owned.Items = {Item};
			auto Validate = [&](const FProject_JLocalProfileSnapshot &S) {
				auto *P = World->SpawnActor<AProject_JPlayerState>();
				FString Error;
				bool OK = UProject_JLocalProfileSubsystem::ValidateMigrationSnapshot(P, S, Error);
				P->Destroy();
				return OK;
			};
			Test->TestTrue(TEXT("Explicit actual ownership passes exact normal restore validation"), Validate(Owned));
			auto Unknown = Owned;
			Unknown.Items[0].Definition = TEXT("UnknownMigrationItem");
			Test->TestFalse(TEXT("Unknown definition rejected before destination"), Validate(Unknown));
			auto Version = Owned;
			++Version.Items[0].DefinitionVersion;
			Test->TestFalse(TEXT("Unknown definition version rejected"), Validate(Version));
			auto Slot = Owned;
			Slot.Items[0].Slot = EProject_JEquipmentSlot::Weapon;
			Test->TestFalse(TEXT("Potion cannot become weapon equipment"), Validate(Slot));
			auto Duplicate = Owned;
			const auto DuplicateItem = Duplicate.Items[0];
			Duplicate.Items.Add(DuplicateItem);
			TArray<uint8> Encoded;
			Test->TestFalse(TEXT("Duplicate instance rejected by native codec"), Duplicate.Encode(Encoded));
			auto Second = Owned;
			Second.Character = FGuid::NewGuid();
			Second.Account = FGuid::NewGuid();
			Second.Items[0].Instance = FGuid::NewGuid();
			Write = Async(EAsyncExecution::ThreadPool, [Directory = Directory, Owned, Second] {
				TArray<FString> Errors;
				auto Check = [&](bool V, const TCHAR *M) {
					if (!V)
						Errors.Add(M);
				};
				const FString Source = Directory / TEXT("MigrationSource.sqlite"),
							  Destination = Directory / TEXT("Migrated.sqlite");
				TArray<FProject_JLocalProfileSnapshot> Ownership{Owned, Second};
				FCommitRequest Request{FGuid::NewGuid(), TEXT("OwnedV1Fixture"), {}};
				FString OwnershipJSON = TEXT("[");
				for (int32 I = 0; I < Ownership.Num(); ++I)
				{
					auto V1 = Ownership[I];
					V1.Format = 1;
					V1.Items.Reset();
					TArray<uint8> P;
					Check(V1.Encode(P), TEXT("Genuine v1 codec has no ownership fields"));
					Request.Mutations.Add({{TEXT("LocalProfile"), V1.Character}, 0, 1, P, false});
					TArray<uint8> Full;
					Check(Ownership[I].Encode(Full), TEXT("Supplied v2 ownership serializes"));
					FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR *>(Full.GetData()), Full.Num());
					if (I)
						OwnershipJSON += TEXT(",");
					OwnershipJSON.Append(Text.Get(), Text.Length());
				}
				OwnershipJSON += TEXT("]");
				Check(FFileHelper::SaveStringToFile(OwnershipJSON, *(Directory / TEXT("Ownership.json"))),
					  TEXT("Save explicit CLI ownership input"));
				FProject_JSQLiteRepository DB(Source);
				Check(DB.Commit(Request).Status == ECommitStatus::Committed,
					  TEXT("Real native v1 source and legacy receipt created"));
				DB.Close();
				{
					FProject_JProfileDatabase Old;
					Check(Old.Open(*Source, ESQLiteDatabaseOpenMode::ReadWrite) &&
							  Old.Execute(TEXT("DROP TABLE profile_receipts;DROP TABLE profile_sessions")) &&
							  Old.SetUserVersion(1),
						  TEXT("Fixture represents genuine previous writer container v1"));
				}
				auto Inspection = FProject_JProfileMigration::Inspect(Source);
				Check(Inspection.IsValid() && Inspection.DatabaseVersion == 1 && Inspection.MissingOwnership.Num() == 2,
					  TEXT("Read-only inspection lists both missing ownership inputs"));
				TArray<FProject_JMigrationRecord> Plan;
				FString Error;
				Check(!FProject_JProfileMigration::Plan(Inspection, {}, Plan, Error),
					  TEXT("No automatic empty inventory synthesis"));
				auto Bad = Ownership;
				Bad[1].Character = Bad[0].Character;
				Check(!FProject_JProfileMigration::Plan(Inspection, Bad, Plan, Error),
					  TEXT("Duplicate ownership character rejected"));
				Bad = Ownership;
				Bad[0].Progression.Experience++;
				Check(!FProject_JProfileMigration::Plan(Inspection, Bad, Plan, Error),
					  TEXT("Migration cannot silently edit progression"));
				Bad = Ownership;
				Bad[1].Items[0].Instance = Bad[0].Items[0].Instance;
				Check(!FProject_JProfileMigration::Plan(Inspection, Bad, Plan, Error),
					  TEXT("Cross-character duplicate item ownership rejected"));
				Check(FProject_JProfileMigration::Plan(Inspection, Ownership, Plan, Error),
					  TEXT("Dry-run plans exact v1 to v2 ownership addition"));
				{
					auto Mixed = Inspection;
					auto Existing = Owned;
					Existing.Character = FGuid::NewGuid();
					FProject_JMigrationRecord Record;
					Record.Kind = TEXT("localprofile");
					Record.Revision = 1;
					Record.Schema = 1;
					Record.Id.Append(reinterpret_cast<const uint8 *>(&Existing.Character), 16);
					Existing.Encode(Record.Payload);
					Mixed.Records.Add(MoveTemp(Record));
					TArray<FProject_JMigrationRecord> Rejected;
					Check(!FProject_JProfileMigration::Plan(Mixed, Ownership, Rejected, Error),
						  TEXT("New ownership cannot duplicate an unchanged v2 character's item instance"));
				}
				Check(!IFileManager::Get().FileExists(*Destination), TEXT("Dry-run creates no result file"));
				FProject_JProfileMigration::FailAfterWritesForTest(1);
				Check(!FProject_JProfileMigration::Create(Source, Destination, Inspection, Plan, Error) &&
						  !IFileManager::Get().FileExists(*Destination),
					  TEXT("Partial migration failure publishes no destination"));
				Check(FProject_JProfileMigration::Inspect(Source).SourceHash == Inspection.SourceHash,
					  TEXT("Partial failure preserves exact original bytes"));
				Check(FProject_JProfileMigration::Create(Source, Destination, Inspection, Plan, Error),
					  TEXT("New v2 file created and semantically compared"));
				Check(FProject_JProfileMigration::Create(Source, Destination, Inspection, Plan, Error),
					  TEXT("Identical repeated migration is idempotent"));
				auto Migrated = FProject_JProfileMigration::Inspect(Destination);
				Check(Migrated.IsValid() && Migrated.DatabaseVersion == 2 && Migrated.MissingOwnership.IsEmpty() &&
						  Migrated.OtherStateHash == Inspection.OtherStateHash,
					  TEXT("All ownership migrated; every legacy receipt preserved"));
				{
					FProject_JSQLiteRepository Refused(Source);
					FRecord R;
					Check(Refused.ReadChecked({TEXT("LocalProfile"), Owned.Character}, R) ==
							  EProject_JRecordRead::Unavailable,
						  TEXT("New runtime refuses old writer layout without an explicit separate-file upgrade"));
					Refused.Close();
					Check(FProject_JProfileMigration::Inspect(Source).SourceHash == Inspection.SourceHash,
						  TEXT("Runtime refusal does not modify legacy source"));
					const FString StorageSource = Directory / TEXT("StorageV1FullSnapshot.sqlite"),
								  StorageResult = Directory / TEXT("StorageUpgrade.sqlite");
					IFileManager::Get().Copy(*StorageSource, *Destination, false);
					{
						FProject_JProfileDatabase Old;
						Check(Old.Open(*StorageSource, ESQLiteDatabaseOpenMode::ReadWrite) && Old.SetUserVersion(1),
							  TEXT("Owned container-v1/full-snapshot-v2 fixture"));
					}
					auto Storage = FProject_JProfileMigration::Inspect(StorageSource);
					TArray<FProject_JMigrationRecord> Upgrade;
					Check(Storage.IsValid() && Storage.DatabaseVersion == 1 && Storage.MissingOwnership.IsEmpty() &&
							  FProject_JProfileMigration::Plan(Storage, {}, Upgrade, Error),
						  TEXT("Container-only upgrade never asks to recreate known item ownership"));
					Check(FProject_JProfileMigration::Create(StorageSource, StorageResult, Storage, Upgrade, Error) &&
							  FProject_JProfileMigration::Inspect(StorageResult).DatabaseVersion == 2,
						  TEXT("New file upgrades writer policy while preserving complete existing profiles"));
				}
				const FString Conflict = Directory / TEXT("Conflict.sqlite");
				FFileHelper::SaveStringToFile(TEXT("preserve me"), *Conflict);
				Check(!FProject_JProfileMigration::Create(Source, Conflict, Inspection, Plan, Error),
					  TEXT("Unrelated destination never overwritten"));
				Check(!FProject_JProfileMigration::Inspect(Conflict).IsValid(), TEXT("Corrupt database rejected"));
				const FString Rollback = Directory / TEXT("Rollback.sqlite");
				Check(FProject_JProfileMigration::RollbackCopy(Source, Destination, Rollback, Error),
					  TEXT("Verified rollback creates a separate original-format file"));
				auto Reverted = FProject_JProfileMigration::Inspect(Rollback);
				Check(Reverted.IsValid() && Reverted.MissingOwnership.Num() == 2 &&
						  Reverted.OtherStateHash == Inspection.OtherStateHash,
					  TEXT("Rollback restores v1 records and exact receipt history"));
				FFileHelper::SaveStringToFile(TEXT("truncated"), *(Source + TEXT("-journal")));
				Check(!FProject_JProfileMigration::Inspect(Source).IsValid(),
					  TEXT("Original sidecar is preserved and blocks offline migration"));
				Check(IFileManager::Get().Delete(*(Source + TEXT("-journal")), false, true),
					  TEXT("Remove only the injected owned malformed sidecar"));
				Check(FProject_JProfileMigration::Inspect(Source).SourceHash == Inspection.SourceHash,
					  TEXT("Every operation preserved original source bytes"));
				const FString BadSchema = Directory / TEXT("UnknownSchema.sqlite");
				IFileManager::Get().Copy(*BadSchema, *Source, false);
				{
					FProject_JProfileDatabase Native;
					Check(Native.Open(*BadSchema, ESQLiteDatabaseOpenMode::ReadWrite) && Native.SetUserVersion(99),
						  TEXT("Create owned unknown-schema fixture"));
				}
				Check(!FProject_JProfileMigration::Inspect(BadSchema).IsValid(), TEXT("Unknown schema is never reset"));
				UE_LOG(LogTemp, Display, TEXT("Migration CLI fixture directory: %s"), *Directory);
				return Errors;
			});
			++Step;
			return false;
		}
		if (!Write.IsReady())
			return false;
		for (const auto &E : Write.Get())
			Test->AddError(E);
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileMigrationTest, "ProjectJ.MMO.Persistence.Migration",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfileMigrationTest::RunTest(const FString &)
{
	AddExpectedError(TEXT("Character data registry is empty."), EAutomationExpectedErrorFlags::Contains, 1);
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	auto *Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	const FString File =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/ProfileMigration") /
										  FGuid::NewGuid().ToString() / TEXT("Seed.sqlite"));
	Profiles->DatabasePath = File;
	TestTrue(TEXT("Migration uses the real existing definition registry"),
			 GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->LoadPrimaryAssetDefinitions());
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FMigrationCases(this, GI, File));
	return true;
}
#endif
