#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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
#include "UObject/StrongObjectPtr.h"

namespace
{
class FGameRetention : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World;
	UProject_JLocalProfileSubsystem *Profiles;
	AProject_JPlayerState *Player = nullptr;
	FString File, Phase;
	int32 Step = 0, Completed = 0, ExpectedCount = 0;
	bool Ready = false, Success = false, Outstanding = false;
	FGuid Potion, FirstKey, LastKey;
	TFuture<bool> Verify;
	double Start = FPlatformTime::Seconds();

  public:
	FGameRetention(FAutomationTestBase *T, UGameInstance *G, FString F, FString P)
		: Test(T), GI(G), World(G->GetWorld()), Profiles(G->GetSubsystem<UProject_JLocalProfileSubsystem>()), File(F),
		  Phase(P)
	{
	}
	~FGameRetention()
	{
		if (IsValid(Player))
			Player->GetInventoryComponent()->CreatePersistentRequest = {};
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void Open()
	{
		Player = World->SpawnActor<AProject_JPlayerState>();
		Ready = Success = false;
		Profiles->OpenProfile(Player, TEXT("GameRetention"), [this](bool OK) {
			Success = OK;
			Ready = true;
		});
	}
	bool Check(bool OK, const TCHAR *M)
	{
		return Test->TestTrue(M, OK);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 600)
		{
			Test->AddError(TEXT("Actual inventory command soak timed out"));
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
			if (!Ready)
				return false;
			if (!Check(Success, TEXT("Real profile component consumer is ready")))
				return true;
			auto *Inventory = Player->GetInventoryComponent();
			if (Phase == TEXT("Read"))
			{
				FString Metadata;
				TArray<FString> Parts;
				Check(FFileHelper::LoadFileToString(Metadata, *(FPaths::GetPath(File) / TEXT("Requests.txt"))),
					  TEXT("Prior process retained its actual request IDs"));
				Metadata.ParseIntoArrayLines(Parts);
				if (!Check(Parts.Num() == 3 && FGuid::Parse(Parts[0], FirstKey) && FGuid::Parse(Parts[1], LastKey) &&
							   FGuid::Parse(Parts[2], Potion),
						   TEXT("Owned restart metadata validates")))
					return true;
				Check(Profiles->GetCommittedRevision(Player) == 12291,
					  TEXT("New process restores 12289 real inventory commands without a redundant save"));
				Step = 5;
				return false;
			}
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			Step = 2;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			auto *Definition = LoadObject<UProject_JConsumableDefinition>(
				nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
			Check(Definition && !Player->GetInventoryComponent()->AddItemDefinition(Definition, 2).InstanceId.IsValid(),
				  TEXT("Actual Add waits for durable ownership"));
			Step = 3;
			return false;
		}
		if (Step == 3)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			auto *Inventory = Player->GetInventoryComponent();
			auto Items = Inventory->GetItemInstances();
			if (!Check(Items.Num() == 1, TEXT("Actual native item was published")))
				return true;
			Potion = Items[0].InstanceId;
			// Observe IDs without replacing admission or storage. This is the normal issuer.
			Inventory->CreatePersistentRequest = [this] {
				LastKey = Profiles->NewRequestId(Player);
				return LastKey;
			};
			Step = 4;
		}
		if (Step == 4)
		{
			auto *Inventory = Player->GetInventoryComponent();
			const double SliceEnd = FPlatformTime::Seconds() + 0.02;
			while (Completed < 12289 && FPlatformTime::Seconds() < SliceEnd)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				if (Profiles->IsCheckpointPending(Player))
				{
					FPlatformProcess::SleepNoStats(0.001f);
					continue;
				}
				if (Outstanding)
				{
					FProject_JItemInstanceData Item;
					if (!Check(Inventory->FindItemInstance(Potion, Item) && Item.StackCount == ExpectedCount,
							   TEXT("Each actual API command publishes only its native confirmed stack")))
						return true;
					Outstanding = false;
					++Completed;
					if (Completed % 4096 == 0)
						UE_LOG(LogTemp, Display,
							   TEXT("Actual inventory command soak confirmed=%d profileRevision=%lld nativeJobs=%d "
									"seconds=%.3f"),
							   Completed, Profiles->GetCommittedRevision(Player), Profiles->GetNativeJobCountForTest(),
							   FPlatformTime::Seconds() - Start);
					if (Completed == 12289)
						break;
				}
				Check(Profiles->GetNativeJobCountForTest() <= 32,
					  TEXT("Actual component soak keeps native jobs bounded"));
				if (Completed == 4096 || Completed == 8192)
					Profiles->LoseNextCommitAcknowledgementForTest(Player);
				ExpectedCount = Completed % 2 == 0 ? 1 : 2;
				if (!Check(Inventory->SetItemStackCount(Potion, ExpectedCount),
						   TEXT("Existing inventory setter continues beyond old receipt caps")))
					return true;
				if (Completed == 0)
					FirstKey = LastKey;
				Outstanding = true;
			}
			if (Completed < 12289)
				return false;
			Inventory->CreatePersistentRequest = {};
			Check(Profiles->GetCommittedRevision(Player) == 12291,
				  TEXT("12289 actual inventory commands persisted exactly once"));
			if (Phase == TEXT("Write"))
			{
				Check(FFileHelper::SaveStringToFile(FirstKey.ToString() + TEXT("\n") + LastKey.ToString() + TEXT("\n") +
														Potion.ToString(),
													*(FPaths::GetPath(File) / TEXT("Requests.txt"))),
					  TEXT("Save process-restart request evidence"));
				Step = 7;
			}
			else
			{
				Profiles->Detach(Player);
				Player->Destroy();
				Open();
				Step = 5;
				return false;
			}
		}
		if (Step == 5)
		{
			if (!Ready)
				return false;
			if (!Check(Success, TEXT("Post-soak profile binding reopens")))
				return true;
			FProject_JItemInstanceData Item;
			Check(Player->GetInventoryComponent()->FindItemInstance(Potion, Item) && Item.StackCount == 1,
				  TEXT("Stable item GUID and final native stack survive reload"));
			FProject_JPersistentEdit Old;
			Old.Kind = EProject_JPersistentEdit::Stack;
			Old.Instance = Potion;
			Old.Amount = 2;
			Old.Request = FirstKey;
			Check(!Profiles->SubmitEdit(Player, Old),
				  TEXT("First pruned old-epoch command cannot become a new mutation"));
			Old.Request = LastKey;
			Check(!Profiles->SubmitEdit(Player, Old),
				  TEXT("Most recent previous-session result also expires on binding replacement"));
			Check(Player->GetInventoryComponent()->SetItemStackCount(Potion, 2),
				  TEXT("Normal actual command continues after restart"));
			Step = 6;
			return false;
		}
		if (Step == 6)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			FProject_JItemInstanceData Item;
			Check(Player->GetInventoryComponent()->FindItemInstance(Potion, Item) && Item.StackCount == 2,
				  TEXT("New-session actual inventory write confirmed"));
			Step = 7;
		}
		if (Step == 7)
		{
			Verify = Async(EAsyncExecution::ThreadPool, [File = File, Write = Phase == TEXT("Write")] {
				FProject_JSQLiteRepository DB(File);
				FProject_JRepositoryUsage U;
				bool OK = DB.GetUsage(U) && U.Records == 2 && U.Receipts == (Write ? 66 : 2);
				DB.Close();
				return OK;
			});
			Step = 8;
			return false;
		}
		if (!Verify.IsReady())
			return false;
		Check(Verify.Get(), TEXT("Actual component soak/restart retains only bounded receipts and two records"));
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGameRetentionTest, "ProjectJ.MMO.Persistence.ActualInventoryLongRun",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGameRetentionTest::RunTest(const FString &)
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJGameRetentionPhase="), Phase);
	if (!Phase.IsEmpty() && Phase != TEXT("Write") && Phase != TEXT("Read"))
	{
		AddError(TEXT("Unknown actual command phase"));
		return false;
	}
	const FString File = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Validation/GameRetention") /
		(Phase.IsEmpty() ? FGuid::NewGuid().ToString() : TEXT("Restart")) / TEXT("Profiles.sqlite"));
	if (Phase == TEXT("Write") && IFileManager::Get().FileExists(*File))
	{
		AddError(TEXT("Preserve old process fixture"));
		return false;
	}
	if (Phase == TEXT("Read") && !IFileManager::Get().FileExists(*File))
	{
		AddError(TEXT("Previous process fixture missing"));
		return false;
	}
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	GI->GetSubsystem<UProject_JLocalProfileSubsystem>()->DatabasePath = File;
	TestTrue(TEXT("Actual primary asset definitions loaded"),
			 GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->LoadPrimaryAssetDefinitions());
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FGameRetention(this, GI, File, Phase));
	return true;
}
#endif
