#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Private/Tests/Project_JMMOArchitectureReceiver.h"
#include "Project_JAbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FCoverageUsage
{
	int64 Revision = 0, Records = 0, Receipts = 0;
	bool Valid = false;
};
class FRuntimeCoverage : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World;
	UProject_JLocalProfileSubsystem *Profiles;
	AProject_JPlayerState *Player;
	UProject_JCharacterClassDefinition *Class;
	UProject_JCharacterAdvancementDefinition *One, *Two;
	UProject_JQuestDefinition *Travel;
	AProject_JProfileTravelTestPawn *Pawn = nullptr;
	APlayerController *Controller = nullptr;
	FString File;
	int32 Step = 0, Stage = -1, XP = 0, Stacks = 0, TravelEvents = 0, CheckpointCalls = 0, AbilityCount = 0;
	bool Ready = false, Success = false;
	int32 LastTravelCount = 0;
	bool BackupPending = false, BackupDone = false, BackupSuccess = false;
	int32 Backups = 0;
	TSet<FString> BackupPaths;
	int64 BeforeRewardRevision = 0, BeforeRewardReceipts = 0;
	FGuid Potion, OldTicket;
	TFuture<FCoverageUsage> Usage;
	FCoverageUsage Initial;
	double Start = FPlatformTime::Seconds(), Measured = 0, Previous = 0, NextCommand = 0;

  public:
	FRuntimeCoverage(FAutomationTestBase *T, UGameInstance *G, AProject_JPlayerState *P,
					 UProject_JCharacterClassDefinition *C, UProject_JCharacterAdvancementDefinition *A,
					 UProject_JCharacterAdvancementDefinition *B, UProject_JQuestDefinition *Q, FString F)
		: Test(T), GI(G), World(G->GetWorld()), Profiles(G->GetSubsystem<UProject_JLocalProfileSubsystem>()), Player(P),
		  Class(C), One(A), Two(B), Travel(Q), File(F)
	{
	}
	~FRuntimeCoverage()
	{
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void ReadUsage()
	{
		Usage = Async(EAsyncExecution::ThreadPool, [File = File, Id = Player->GetCharacterId().Value] {
			FProject_JSQLiteRepository DB(File);
			FProject_JRepositoryUsage U;
			ProjectJ::MMO::FRecord R;
			FCoverageUsage V;
			V.Valid = DB.GetUsage(U) && DB.Read({TEXT("LocalProfile"), Id}, R);
			V.Revision = R.Revision;
			V.Records = U.Records;
			V.Receipts = U.Receipts;
			DB.Close();
			return V;
		});
	}
	void Check(bool OK, const TCHAR *M)
	{
		Test->TestTrue(M, OK);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Start > 100)
		{
			Test->AddError(TEXT("Actual gameplay frequency fixture timed out"));
			return true;
		}
		Check(Profiles->GetNativeJobCountForTest() <= 32,
			  TEXT("Actual gameplay native jobs stay within admission bound"));
		auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
		auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
		auto *I = Player->GetInventoryComponent();
		if (Step == 0)
		{
			auto *Reentering = Player;
			Reentering->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.AddLambda([this, Reentering] {
				if (Profiles->IsReady(Reentering))
					return;
				const auto *Quests = Reentering->FindComponentByClass<UProject_JQuestComponent>();
				Check(Reentering->GetInventoryComponent()->GetItemInstances().Num() == 1 &&
						  Quests->States.ContainsByPredicate(
							  [this](const auto &V) { return V.QuestId == Travel->QuestId && V.bClaimed; }),
					  TEXT("First progression load publication observes complete inventory and claimed quest"));
				Check(!Reentering->GetInventoryComponent()->SetItemStackCount(Potion, 1),
					  TEXT("Initial load publication cannot bypass persistent admission"));
			});
			Profiles->OpenProfile(Player, TEXT("RuntimeCoverage"), [this](bool OK) {
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
			if (!Success)
			{
				Test->AddError(TEXT("Runtime coverage could not open"));
				return true;
			}
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			P->InitializeDefaults(nullptr, nullptr, 1);
			++Step;
			return false;
		}
		if (Step < 8 && Profiles->IsCheckpointPending(Player))
			return false;
		if (Step == 2)
		{
			Check(P->InitializeClass(Class) && !P->GetState().ClassDefinition,
				  TEXT("Existing class API admits atomic command before grant/publication"));
			++Step;
			return false;
		}
		if (Step == 3)
		{
			Check(P->GetState().ClassDefinition == Class, TEXT("Confirmed class installed"));
			AbilityCount = Player->GetProjectJAbilitySystemComponent()->GetActivatableAbilities().Num();
			Check(P->CanApplyAdvancement(One) && P->ApplyAdvancement(One) && P->GetAcquiredAdvancements().IsEmpty(),
				  TEXT("Existing advancement API commits before history/grants"));
			++Step;
			return false;
		}
		if (Step == 4)
		{
			Check(P->GetAcquiredAdvancements() == TArray<FName>{One->AdvancementId},
				  TEXT("First advancement atomically published"));
			Check(P->CanApplyAdvancement(Two) && P->ApplyAdvancement(Two),
				  TEXT("Ordered replacement advancement admitted"));
			++Step;
			return false;
		}
		if (Step == 5)
		{
			Check(P->GetAcquiredAdvancements() == TArray<FName>{One->AdvancementId, Two->AdvancementId} &&
					  P->GetState().Advancement == Two &&
					  Player->GetProjectJAbilitySystemComponent()->GetActivatableAbilities().Num() == AbilityCount,
				  TEXT("History order, active definition and GAS lease count agree"));
			Check(!P->ApplyAdvancement(One) && !P->SetLevel(12),
				  TEXT("Duplicate advancement and developer level override refused; XP remains official path"));
			auto *Def = LoadObject<UProject_JConsumableDefinition>(
				nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
			Check(Def && !I->AddItemDefinition(Def, 2, 1).InstanceId.IsValid(),
				  TEXT("Add remains unconfirmed until asynchronous publication"));
			++Step;
			return false;
		}
		if (Step == 6)
		{
			auto Items = I->GetItemInstances();
			if (!Test->TestTrue(TEXT("Committed Add publishes actual ownership"), Items.Num() == 1))
				return true;
			Potion = Items[0].InstanceId;
			OldTicket = Profiles->NewRequestId(Player);
			ReadUsage();
			++Step;
			return false;
		}
		if (Step == 7)
		{
			if (!Usage.IsReady())
				return false;
			Initial = Usage.Get();
			Check(Initial.Valid && Initial.Records == 2, TEXT("Actual profile baseline has two records"));
			Measured = Previous = FPlatformTime::Seconds();
			NextCommand = 30;
			Step = 8;
			return false;
		}
		if (Step == 8)
		{
			double Now = FPlatformTime::Seconds(), Elapsed = Now - Measured;
			float Delta = float(FMath::Clamp(Now - Previous, 0.0, 0.1));
			Previous = Now;
			int32 NextStage = Elapsed < 10 ? 0 : Elapsed < 30 ? 1 : Elapsed < 40 ? 2 : Elapsed < 50 ? 3 : 4;
			if (NextStage != Stage)
			{
				if (Stage >= 0)
					UE_LOG(LogTemp, Display,
						   TEXT("Actual profile API phase=%d elapsed=%.3f profileRevision=%lld XPcommands=%d "
								"stackCommands=%d travelNotifications=%d unchangedCheckpointCalls=%d"),
						   Stage, Elapsed, Profiles->GetCommittedRevision(Player), XP, Stacks, TravelEvents,
						   CheckpointCalls);
				Stage = NextStage;
				if (Stage == 1)
				{
					Controller = World->SpawnActor<APlayerController>();
					Controller->SetPlayerState(Player);
					Player->SetOwner(Controller);
					Pawn = World->SpawnActor<AProject_JProfileTravelTestPawn>();
					Controller->Possess(Pawn);
					Pawn->Moving = true;
					Q->OnChanged.AddLambda([this, Q] {
						if (const auto *State = Q->States.FindByPredicate(
								[this](const auto &V) { return V.QuestId == Travel->QuestId; });
							State && State->Count != LastTravelCount)
						{
							++TravelEvents;
							LastTravelCount = State->Count;
						}
					});
				}
				if (Stage == 2)
				{
					Pawn->Moving = false;
					NextCommand = Elapsed;
				}
			}
			if (Stage == 0)
			{
				Profiles->RequestCheckpoint(Player);
				++CheckpointCalls;
				Check(Profiles->GetCommittedRevision(Player) == Initial.Revision,
					  TEXT("Repeated identical state never writes a new checkpoint"));
			}
			if (Pawn && Pawn->Moving)
				Pawn->AddActorWorldOffset(Pawn->GetVelocity() * Delta);
			World->Tick(LEVELTICK_All, Delta);
			if ((Stage == 2 || Stage == 3) && Elapsed >= NextCommand && !Profiles->IsCheckpointPending(Player))
			{
				if (Stage == 2)
				{
					if (P->GrantExperience(1))
						++XP;
				}
				else
				{
					auto Items = I->GetItemInstances();
					const auto *Item = Items.FindByPredicate([&](const auto &V) { return V.InstanceId == Potion; });
					if (Item && I->SetItemStackCount(Potion, Item->StackCount == 1 ? 2 : 1))
						++Stacks;
				}
				NextCommand = Elapsed + 0.5;
			}
			if (Stage == 4 && !Profiles->IsCheckpointPending(Player))
			{
				Check(XP >= 15 && Stacks >= 15 && TravelEvents >= 20,
					  TEXT("Wall-clock run exercised real progression, item commands and travel sampler"));
				ReadUsage();
				++Step;
			}
			return false;
		}
		if (Step == 9)
		{
			if (!Usage.IsReady())
				return false;
			auto Actual = Usage.Get();
			Check(Actual.Valid && Actual.Records == 2 && Actual.Receipts <= 66,
				  TEXT("Gameplay frequency run keeps records and receipt window bounded"));
			UE_LOG(LogTemp, Display,
				   TEXT("Actual wall-clock profile measurement seconds=%.3f records=%lld receipts=%lld "
						"revisionDelta=%lld xp=%d stack=%d travel=%d unchangedCalls=%d (controlled movement, not an "
						"extrapolated 34-minute lifetime)"),
				   FPlatformTime::Seconds() - Measured, Actual.Records, Actual.Receipts,
				   Actual.Revision - Initial.Revision, XP, Stacks, TravelEvents, CheckpointCalls);
			BeforeRewardRevision = Actual.Revision;
			BeforeRewardReceipts = Actual.Receipts;
			Q->RecordObjective(Travel->ObjectiveEvent, Travel->RequiredCount);
			Step = 10;
			return false;
		}
		if (Step == 10)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			Check(Q->Claim(Travel->QuestId) && P->GetExperience() == XP, TEXT("Reward claim waits for native commit"));
			Step = 11;
			return false;
		}
		if (Step == 11)
		{
			if (Profiles->IsCheckpointPending(Player))
				return false;
			Check(P->GetExperience() == XP + 7, TEXT("Measured reward publishes once"));
			ReadUsage();
			Step = 12;
			return false;
		}
		if (Step == 12)
		{
			if (!Usage.IsReady())
				return false;
			auto Reward = Usage.Get();
			Check(Reward.Valid && Reward.Revision == BeforeRewardRevision + 2 &&
					  Reward.Receipts == BeforeRewardReceipts + 1,
				  TEXT("Final objective checkpoint replaces one result; claim adds one economy receipt and one profile "
					   "revision"));
			UE_LOG(LogTemp, Display,
				   TEXT("Actual category delta: finalObjective+claim records=0 receipts=%lld profileRevisions=%lld "
						"claimXP=7"),
				   Reward.Receipts - BeforeRewardReceipts, Reward.Revision - BeforeRewardRevision);
			Controller->UnPossess();
			Pawn->Destroy();
			Profiles->Detach(Player);
			Player->Destroy();
			Player = World->SpawnActor<AProject_JPlayerState>();
			Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions = {Travel};
			Ready = false;
			Success = false;
			auto *Reentering = Player;
			Reentering->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.AddLambda([this, Reentering] {
				if (Profiles->IsReady(Reentering))
					return;
				const auto *Quests = Reentering->FindComponentByClass<UProject_JQuestComponent>();
				Check(Reentering->GetInventoryComponent()->GetItemInstances().Num() == 1 &&
						  Quests->States.ContainsByPredicate(
							  [this](const auto &V) { return V.QuestId == Travel->QuestId && V.bClaimed; }),
					  TEXT("First progression load publication observes complete inventory and claimed quest"));
				Check(!Reentering->GetInventoryComponent()->SetItemStackCount(Potion, 1),
					  TEXT("Initial load publication cannot bypass persistent admission"));
			});
			Profiles->OpenProfile(Player, TEXT("RuntimeCoverage"), [this](bool OK) {
				Ready = true;
				Success = OK;
			});
			++Step;
			return false;
		}
		if (Step == 13)
		{
			if (!Ready)
				return false;
			P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			Check(Success && P->GetExperience() == XP + 7 &&
					  P->GetAcquiredAdvancements() == TArray<FName>{One->AdvancementId, Two->AdvancementId} &&
					  Player->GetInventoryComponent()->GetItemInstances().Num() == 1,
				  TEXT("New binding restores class, ordered history, XP and item ownership"));
			FProject_JPersistentEdit Old;
			Old.Kind = EProject_JPersistentEdit::Experience;
			Old.Request = OldTicket;
			Old.Amount = 1;
			Check(!Profiles->SubmitEdit(Player, Old) && Profiles->IsReady(Player),
				  TEXT("Old-session ticket rejected without poisoning current binding"));
			Step = 14;
			return false;
		}
		if (Backups < 9)
		{
			if (!BackupPending)
			{
				BackupPending = true;
				BackupDone = false;
				Profiles->RequestBackup([this](bool OK, FString Path) {
					BackupSuccess = OK;
					BackupDone = true;
					if (OK)
						BackupPaths.Add(Path);
				});
			}
			if (!BackupDone)
				return false;
			Check(BackupSuccess == (Backups < 8),
				  TEXT("Eight backups retained; ninth refused without deleting or overwriting user data"));
			++Backups;
			BackupPending = false;
			return false;
		}
		Check(BackupPaths.Num() == 8, TEXT("Bounded backup admission preserves eight distinct verified files"));

		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileRuntimeCoverageTest,
								 "ProjectJ.MMO.Persistence.RuntimeFrequencyAndProgression",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfileRuntimeCoverageTest::RunTest(const FString &)
{
	AddExpectedError(TEXT("Backup admission full \\(8 retained files\\)"), EAutomationExpectedErrorFlags::Contains, 1);
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	auto *Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!TestTrue(TEXT("Existing registry loaded"), Data->LoadPrimaryAssetDefinitions()))
		return false;
	auto *Class = LoadObject<UProject_JCharacterClassDefinition>(
		nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
	if (!Class)
		return false;
	auto *One = NewObject<UProject_JCharacterAdvancementDefinition>(Data);
	One->AdvancementId = TEXT("OwnedTestAdvanceOne");
	One->BaseClass = Class;
	One->RequiredLevel = 1;
	auto *Two = NewObject<UProject_JCharacterAdvancementDefinition>(Data);
	Two->AdvancementId = TEXT("OwnedTestAdvanceTwo");
	Two->BaseClass = Class;
	Two->RequiredLevel = 1;
	Two->RequiredAdvancementIds = {One->AdvancementId};
	Two->AbilityGrantPolicy = EProject_JAdvancementAbilityGrantPolicy::ReplacePreviousAdvancement;
	Data->AdvancementMap.Add(One->AdvancementId, One);
	Data->AdvancementMap.Add(Two->AdvancementId, Two);
	auto *Player = World->SpawnActor<AProject_JPlayerState>();
	auto *Travel = NewObject<UProject_JQuestDefinition>(Player);
	Travel->QuestId = TEXT("OwnedTravelMeasurement");
	Travel->ObjectiveEvent = TEXT("OwnedTravel");
	Travel->bTravelObjective = true;
	Travel->RequiredCount = 100000;
	Travel->RewardExperience = 7;
	Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions = {Travel};
	const FString File =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/ProfileRuntimeCoverage") /
										  FGuid::NewGuid().ToString() / TEXT("Profiles.sqlite"));
	GI->GetSubsystem<UProject_JLocalProfileSubsystem>()->DatabasePath = File;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FRuntimeCoverage(this, GI, Player, Class, One, Two, Travel, File));
	return true;
}
#endif
