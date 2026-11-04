#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Crafting/Project_JCraftingJobValues.h"
#include "Components/StaticMeshComponent.h"
#include "Crafting/Project_JCraftingPlan.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "Crafting/Project_JCraftingStation.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JSpellbladeCharacter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include <atomic>

namespace
{
struct FDurableProbe
{
	int32 Calls = 0;
	bool bGameThread = false, bOwnerGone = false, bReentryRejected = false;
	FProject_JProfileDurableResult Value;
};
struct FCraftWorkerProbe
{
	std::atomic<int32> Calls{0};
	std::atomic<bool> WrongThread{false}, TimedOut{false};
	FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
	~FCraftWorkerProbe() { FPlatformProcess::ReturnSynchEventToPool(Entered); FPlatformProcess::ReturnSynchEventToPool(Release); }
};
class FCraftingExecution : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	TStrongObjectPtr<UProject_JCraftingRecipeDefinition> Recipe;
	TStrongObjectPtr<UProject_JQuestDefinition> Quest;
	UWorld* World;
	UProject_JLocalProfileSubsystem* Profiles;
	AProject_JPlayerState* Player;
	AProject_JCraftingStation* Station = nullptr;
	UProject_JItemDefinition *Health, *Mana;
	TFunction<bool(int32)> ConfigureRecipe;
	FProject_JPersistentEdit Replay, TimedStart, TimedCollect;
	FGuid TimedJob;
	int64 TimedDue = 0;
	int32 TimedJobPublications = 0;
	double TimedSummaryRemaining = 0;
	TWeakObjectPtr<UProject_JCraftingJobsComponent> TimedObservedJobs;
	FDelegateHandle TimedNotifyHandle;
	TWeakObjectPtr<UProject_JCraftingJobsComponent> ClientObservedJobs;
	FDelegateHandle ClientRequestNotifyHandle;
	FGuid ClientCancelRequest;
	double ClientRequestObservedAt = 0;
	int32 ClientPendingNotifications = 0, ClientConfirmedNotifications = 0;
	int32 WriteFailureHealth = 0, WriteFailureMana = 0;
	TWeakObjectPtr<AProject_JPlayerState> TimedRetiredOwner;
	int32 Step = 0, ProgressionPublications = 0;
	int64 Revision = 0;
	bool Done = false, Success = false, First = false, OldCompletion = false;
	TSharedRef<FDurableProbe, ESPMode::ThreadSafe> Probe = MakeShared<FDurableProbe, ESPMode::ThreadSafe>();
	TSharedRef<FDurableProbe, ESPMode::ThreadSafe> RetiredProbe = MakeShared<FDurableProbe, ESPMode::ThreadSafe>();
	TSharedRef<FCraftWorkerProbe, ESPMode::ThreadSafe> WorkerProbe = MakeShared<FCraftWorkerProbe, ESPMode::ThreadSafe>();
	int32 PlansBeforeReplay = 0;
	TWeakObjectPtr<AProject_JPlayerState> RetiredOwner;
	double Started = FPlatformTime::Seconds();
public:
	FCraftingExecution(FAutomationTestBase* T, UGameInstance* G, AProject_JPlayerState* P,
		UProject_JCraftingRecipeDefinition* R, UProject_JItemDefinition* H, UProject_JItemDefinition* M,
		TFunction<bool(int32)> Configure)
		: Test(T), GI(G), Recipe(R), World(G->GetWorld()), Profiles(G->GetSubsystem<UProject_JLocalProfileSubsystem>()),
		Player(P), Health(H), Mana(M), ConfigureRecipe(MoveTemp(Configure))
    {
        Quest.Reset(NewObject<UProject_JQuestDefinition>(G)); Quest->QuestId = TEXT("OwnedCraftingCompound");
        FProject_JQuestObjectiveDefinition Gather; Gather.ObjectiveId = TEXT("Gather"); Gather.Kind = EProject_JQuestObjectiveKind::GatherItem;
        Gather.TargetId = H->ItemId; Gather.RequiredCount = 100;
        FProject_JQuestObjectiveDefinition Craft; Craft.ObjectiveId = TEXT("Craft"); Craft.Kind = EProject_JQuestObjectiveKind::CraftRecipe;
        Craft.TargetId = R->RecipeId; Craft.TargetVersion = R->SchemaVersion; Craft.RequiredCount = 20;
        Quest->Objectives = {Gather, Craft};
        Player->FindComponentByClass<UProject_JQuestComponent>()->EnsureDefinitionsResolved();
        Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions.Add(Quest.Get());
    }
	~FCraftingExecution()
	{
		if (TimedObservedJobs.IsValid()) TimedObservedJobs->OnChanged.Remove(TimedNotifyHandle);
		if (ClientObservedJobs.IsValid()) ClientObservedJobs->OnRequestChanged.Remove(ClientRequestNotifyHandle);
		WorkerProbe->Release->Trigger();
		Profiles->HoldCommandCompletionsForTest(false);
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	void Check(bool OK, const TCHAR* Message) { Test->TestTrue(Message, OK); }
	void ObserveWorker()
	{
		Profiles->SetCommandStageHookForTest(Player, [Probe = WorkerProbe](const TCHAR* Point)
		{
			if (FCString::Strcmp(Point, TEXT("BeforeCraftPlan")) != 0) return;
			if (IsInGameThread()) Probe->WrongThread.store(true, std::memory_order_relaxed);
			if (Probe->Calls.fetch_add(1, std::memory_order_relaxed) == 0)
			{
				Probe->Entered->Trigger();
				if (!Probe->Release->Wait(5000)) Probe->TimedOut.store(true, std::memory_order_relaxed);
			}
		});
	}
	int32 Count(UProject_JItemDefinition* Definition) const
	{
		int32 Result = 0;
		for (const auto& Item : Player->GetInventoryComponent()->GetItemInstances())
			if (Item.ItemDef == Definition) Result += Item.StackCount;
		return Result;
	}
    void CheckProgress(int32 Expected)
    {
        const auto Snapshot = Player->FindComponentByClass<UProject_JQuestComponent>()->CaptureSnapshot();
        const auto* State = Snapshot.States.FindByPredicate([this](const auto& S) { return S.QuestId == Quest->QuestId; });
        Check(State && State->Count == 0 && State->GetObjectiveCount(TEXT("Craft")) == Expected && State->GetObjectiveCount(TEXT("Gather")) == 0,
            TEXT("The same committed snapshot preserves exact craft progress; generic inventory Add never counts as gathering"));
    }
	FProject_JPersistentEdit Prepare(EProject_JPersistentEdit Kind = EProject_JPersistentEdit::Craft)
	{
		ProjectJ::Crafting::FRecipeValues Values; FString Rules;
		Check(Recipe->Capture(*GI->GetSubsystem<UProject_JCharacterDataSubsystem>(), Values, Rules), TEXT("Prepare captures canonical recipe rules"));
		FProject_JPersistentEdit Edit; Edit.Kind = Kind;
		Edit.Request = Profiles->NewRequestId(Player); Edit.CraftingRecipe = Recipe->RecipeId;
		Edit.CraftingRecipeVersion = Recipe->SchemaVersion; Edit.CraftingRulesDigest = MoveTemp(Rules);
		return Edit;
	}
	void Submit(const FProject_JPersistentEdit& Edit)
	{
		Done = Success = First = false; *Probe = FDurableProbe{};
		Check(Profiles->SubmitEdit(Player, Edit, [this](bool OK, bool Applied) {
			Done = true; Success = OK; First = Applied;
		}, [Receiver = Probe, Provider = TWeakObjectPtr<UProject_JLocalProfileSubsystem>(Profiles),
			Owner = TWeakObjectPtr<AProject_JPlayerState>(Player), Edit](const FProject_JProfileDurableResult& Value)
		{
			++Receiver->Calls; Receiver->bGameThread = IsInGameThread(); Receiver->Value = Value;
			Receiver->bReentryRejected = Provider.IsValid() && !Provider->SubmitEdit(Owner.Get(), Edit);
		}),
			TEXT("Craft enters the existing persistent command worker"));
	}

	FProject_JPersistentEdit JobEdit(EProject_JPersistentEdit Kind, FGuid JobId)
	{
		FProject_JPersistentEdit Edit; Edit.Kind = Kind; Edit.CraftingJob = JobId;
		Edit.Request = Profiles->NewRequestId(Player); return Edit;
	}
	void EnsureTimedAvatar()
	{
		if (Player->GetPawn()) return;
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Controller = World->SpawnActor<APlayerController>(Params);
		auto* Avatar = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
		Player->SetOwner(Controller); Controller->SetPlayerState(Player); Avatar->SetPlayerState(Player); Controller->Possess(Avatar);
		Avatar->SetActorLocation(FVector(0, 0, 100));
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 60) { Test->AddError(TEXT("Craft transaction fixture exceeded60s")); return true; }
		if (Step == 0)
		{
			Profiles->OpenProfile(Player, TEXT("CraftingExecution"), [this](bool OK) { Done = true; Success = OK; });
			++Step; return false;
		}
		if (Step == 1)
		{
			if (!Done) return false;
			if (!Success) { Test->AddError(TEXT("Optional invalid recipe blocked profile opening")); return true; }
			Check(ConfigureRecipe(1), TEXT("Optional recipes publish after explicit successful refresh"));
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			++Step; return false;
		}
		if (Step != 4 && Step != 9 && Step != 31 && Step != 1011 && Profiles->IsCheckpointPending(Player)) return false;
		if (Step == 2)
		{
            auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
            if (!Quests->States.ContainsByPredicate([this](const auto& S) { return S.QuestId == Quest->QuestId; }))
            {
                if (!Quests->Accept(Quest->QuestId)) { Test->AddError(TEXT("Compound acceptance was not admitted after initialization drained")); return true; }
                return false;
            }
			Player->GetInventoryComponent()->AddItemDefinition(Health, 6, 1);
			Check(Count(Health) == 0, TEXT("Material grant waits for durable confirmation"));
			++Step; return false;
		}
		if (Step == 3)
		{
            CheckProgress(0);
			Check(Count(Health) == 6 && Count(Mana) == 0, TEXT("Material preparation uses existing inventory owner"));
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Controller = World->SpawnActor<APlayerController>(Params);
			auto* Avatar = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
			Player->SetOwner(Controller); Controller->SetPlayerState(Player); Avatar->SetPlayerState(Player); Controller->Possess(Avatar);
			Avatar->SetActorLocation(FVector(0, 0, 100));
			Station = World->SpawnActor<AProject_JCraftingStation>(Params);
			Station->SetActorLocation(FVector(150, 0, 100));
			Station->RecipeId = Recipe->RecipeId;
			Station->BenchVisual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Check(Station->BenchVisual->GetStaticMesh() && Station->CanInteract_Implementation(Avatar), TEXT("Visible native bench is eligible for real interaction"));
			Station->SetActorLocation(FVector(1000, 0, 100));
			Check(!Station->TryCraft(Avatar), TEXT("Direct server interaction enforces range without relying on selection RPC"));
			Station->SetActorLocation(FVector(150, 0, 100));
			Revision = Profiles->GetCommittedRevision(Player);
			ObserveWorker();
			Player->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.AddLambda([this] { ++ProgressionPublications; });
			auto* Function = Avatar->FindFunction(TEXT("ServerTryInteract"));
			if (!Function) { Test->AddError(TEXT("Existing reflected interaction RPC is missing")); return true; }
			Avatar->ProcessEvent(Function, nullptr);
			Check(Profiles->IsCheckpointPending(Player) && Count(Health) == 6 && Count(Mana) == 0,
				TEXT("Actual reflected interaction selects the bench and admits one atomic edit"));
			Step = 31; return false;
		}
		if (Step == 31)
		{
			if (!WorkerProbe->Entered->Wait(0)) return false;
			Check(!WorkerProbe->TimedOut.load(std::memory_order_relaxed), TEXT("Accepted craft reaches the existing worker before value planning"));
			Recipe->Outputs[0].CountPerBatch = 2;
			Check(ConfigureRecipe(1), TEXT("Current canonical recipe may change after the accepted value capture"));
			WorkerProbe->Release->Trigger();
			Step = 5; return false;
		}
		if (Step == 5)
		{
            CheckProgress(1);
			Check(Count(Health) == 4 && Count(Mana) == 1 && Profiles->GetCommittedRevision(Player) == Revision + 1,
				TEXT("One profile CAS consumes2 health and grants the frozen1 mana even after the live recipe changed"));
			Check(WorkerProbe->Calls.load(std::memory_order_relaxed) == 1 && !WorkerProbe->WrongThread.load(std::memory_order_relaxed)
				&& !WorkerProbe->TimedOut.load(std::memory_order_relaxed), TEXT("Actual value planning runs once off GT without a new worker queue"));
			Recipe->Outputs[0].CountPerBatch = 1;
			Check(ConfigureRecipe(1), TEXT("Canonical recipe restored before preparing a new logical intent"));
			Replay = Prepare(); Revision = Profiles->GetCommittedRevision(Player);
			Profiles->LoseNextCommitAcknowledgementForTest(Player); Profiles->HoldCommandCompletionsForTest(true);
			Submit(Replay);
			Check(!Profiles->SubmitEdit(Player, Replay), TEXT("Pending atomic edit retains existing single-command admission"));
			Step = 4; return false;
		}
		if (Step == 4)
		{
			if (!Profiles->GetHeldCompletionCountForTest()) return false;
            CheckProgress(1);
			Check(Count(Health) == 4 && Count(Mana) == 1 && !Done, TEXT("Unacknowledged commit never partially publishes materials or outputs"));
			Check(Probe->Calls == 1 && Probe->bGameThread && Probe->bReentryRejected && Probe->Value.Request == Replay.Request &&
				Probe->Value.Outcome == EProject_JProfileDurableOutcome::Committed && Probe->Value.StoreStatus == ProjectJ::MMO::ECommitStatus::Replayed,
				TEXT("Durable lost-ack evidence is delivered once on GT before held live publication"));
			Station->Destroy(); Check(ConfigureRecipe(2) && !GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->GetCraftingRecipe(Recipe->RecipeId, Recipe->SchemaVersion) && Profiles->IsReady(Player), TEXT("Optional recipe removal takes effect without revoking the admitted profile"));
			Profiles->HoldCommandCompletionsForTest(false);
			Check(Done && Success && First && Count(Health) == 2 && Count(Mana) == 2,
				TEXT("Lost ack retries once; station retirement cannot cancel an accepted durable transaction"));
            CheckProgress(2);
			Submit(Replay); Step = 6; return false;
		}
		if (Step == 6)
		{
            CheckProgress(2);
			Check(Done && Success && !First && Count(Health) == 2 && Count(Mana) == 2 &&
				Profiles->GetCommittedRevision(Player) == Revision + 1,
				TEXT("Known exact receipt replays after recipe removal without another charge or grant"));
			Check(Probe->Calls == 1 && Probe->bGameThread && Probe->Value.Outcome == EProject_JProfileDurableOutcome::Committed &&
				Probe->Value.StoreStatus == ProjectJ::MMO::ECommitStatus::Replayed, TEXT("Known receipt has independent committed evidence"));
			PlansBeforeReplay = WorkerProbe->Calls.load(std::memory_order_relaxed);
			Check(ConfigureRecipe(1), TEXT("Current recipe is valid for the exact known-receipt replay"));
			Submit(Replay); Step = 61; return false;
		}
		if (Step == 61)
		{
			CheckProgress(2);
			Check(Done && Success && !First && WorkerProbe->Calls.load(std::memory_order_relaxed) == PlansBeforeReplay
				&& Count(Health) == 2 && Count(Mana) == 2, TEXT("Known receipt skips value planning and GUID allocation even when the current recipe is valid"));
			Check(ConfigureRecipe(2), TEXT("Recipe removal restored for the different-intent receipt probe"));
			auto Wrong = Replay; Wrong.CraftingBatches = 2; Submit(Wrong); Step = 7; return false;
		}
		if (Step == 7)
		{
            CheckProgress(2);
			Check(Done && !Success && Profiles->IsReady(Player) && Count(Health) == 2 && Count(Mana) == 2,
				TEXT("Same key with different craft payload is rejected without faulting current profile"));
			Check(Probe->Calls == 1 && Probe->Value.bReceiptQueried && Probe->Value.Outcome == EProject_JProfileDurableOutcome::Unknown &&
				Probe->Value.ReceiptStatus == ProjectJ::MMO::ECommitStatus::KeyReused, TEXT("Different-payload receipt is not proof of non-payment"));
			Check(ConfigureRecipe(1), TEXT("Valid current recipes can be explicitly restored"));
			Replay = Prepare(); Profiles->HoldCommandCompletionsForTest(true);
			RetiredOwner = Player;
			Check(Profiles->SubmitEdit(Player, Replay, [this](bool, bool) { OldCompletion = true; },
				[Receiver = RetiredProbe, Owner = RetiredOwner, Provider = TWeakObjectPtr<UProject_JLocalProfileSubsystem>(Profiles), Retry = Replay](const FProject_JProfileDurableResult& Value)
				{
					++Receiver->Calls; Receiver->bGameThread = IsInGameThread(); Receiver->Value = Value;
					Receiver->bOwnerGone = !Owner.IsValid() || Owner->IsActorBeingDestroyed();
					Receiver->bReentryRejected = Provider.IsValid() && !Provider->SubmitEdit(Owner.Get(), Retry);
					CollectGarbage(RF_NoFlags, true); // The producer resolves its weak subsystem only after this observer returns.
				}), TEXT("Final craft accepted before session retirement"));
			Profiles->Detach(Player);
			Check(Player->Destroy(), TEXT("Real owner destruction happens before queued GT outcome delivery"));
			Player = nullptr; CollectGarbage(RF_NoFlags, true);
			Step = 9; return false;
		}
		if (Step == 9)
		{
			if (!Profiles->GetHeldCompletionCountForTest()) return false;
			auto* Old = RetiredOwner.Get();
			Check(RetiredProbe->Calls == 1 && RetiredProbe->bGameThread && RetiredProbe->bOwnerGone && RetiredProbe->bReentryRejected && RetiredProbe->Value.Request == Replay.Request &&
				RetiredProbe->Value.Outcome == EProject_JProfileDurableOutcome::Committed, TEXT("Native value receiver gets one durable outcome after owner Destroy/GC"));
			Profiles->Detach(Old);
			Check(ConfigureRecipe(2), TEXT("Recipe can be removed before a profile with crafted items reopens"));
			Player = World->SpawnActor<AProject_JPlayerState>(); Done = Success = false;
            Player->FindComponentByClass<UProject_JQuestComponent>()->EnsureDefinitionsResolved();
        Player->FindComponentByClass<UProject_JQuestComponent>()->Definitions.Add(Quest.Get());
			Profiles->OpenProfile(Player, TEXT("CraftingExecution"), [this](bool OK) { Done = true; Success = OK; });
			Profiles->HoldCommandCompletionsForTest(false);
			Check(!OldCompletion && !Profiles->IsReady(Old) && !Profiles->SubmitEdit(Old, Replay),
				TEXT("Retired binding neither receives late publication nor admits new commands"));
			Step = 10; return false;
		}
		if (Step == 10)
		{
			if (!Done) return false;
            CheckProgress(3);
			Check(Success && Count(Health) == 0 && Count(Mana) == 3 &&
				!GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->GetCraftingRecipe(Recipe->RecipeId, Recipe->SchemaVersion),
				TEXT("Reconnection restores accepted crafted items even when their recipe was removed"));
			Check(!Profiles->SubmitEdit(Player, Replay), TEXT("Old session request identity cannot authorize a new-session craft"));
			Check(ConfigureRecipe(1), TEXT("Current recipe explicitly restored for missing-material probe"));
			ObserveWorker();
			auto Missing = Prepare(); Submit(Missing); ++Step; return false;
		}
		if (Step == 11)
		{
            CheckProgress(3);
			Check(Done && !Success && Profiles->IsReady(Player) && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("Missing materials cause no output or partial mutation and keep profile healthy"));
			Check(Probe->Calls == 1 && Probe->bGameThread && Probe->Value.bReceiptQueried &&
				Probe->Value.Outcome == EProject_JProfileDurableOutcome::ProvenAbsent && Probe->Value.ReceiptStatus == ProjectJ::MMO::ECommitStatus::Invalid,
				TEXT("Invalid plan reports only exact current-session receipt absence"));
			Player->GetInventoryComponent()->AddItemDefinition(Health, 2, 1); ++Step; return false;
		}
		if (Step == 12)
		{
            CheckProgress(3);
			Check(Count(Health) == 2, TEXT("Failure probe receives ordinary committed materials"));
			Recipe->Outputs[0].CountPerBatch = 1000000;
			Check(ConfigureRecipe(1), TEXT("Bounded bulk-output recipe is valid data"));
			Submit(Prepare()); ++Step; return false;
		}
		if (Step == 13)
		{
            CheckProgress(3);
			Check(Done && !Success && Profiles->IsReady(Player) && Count(Health) == 2 && Count(Mana) == 3,
				TEXT("Actual profile rejects output capacity exhaustion before consuming valid materials"));
			Recipe->Outputs[0].CountPerBatch = 1;
			Check(ConfigureRecipe(1), TEXT("Normal output balance restored for storage-failure probe"));
			Recipe->DurationSecondsPerBatch = 2;
			Check(ConfigureRecipe(1), TEXT("The existing canonical recipe opts into bounded timed production"));
			Step = 100; return false;
		}
		if (Step == 100)
		{
			EnsureTimedAvatar();
			Station = World->SpawnActor<AProject_JCraftingStation>();
			Station->SetActorLocation(FVector(150, 0, 100)); Station->RecipeId = Recipe->RecipeId;
			Check(Station->TryCraft(Player->GetPawn()), TEXT("Real bench admits timed start through the existing profile transaction"));
			Step = 101; return false;
		}
		if (Step == 101)
		{
			auto* Jobs = Player->GetCraftingJobsComponent(); TimedJob = Jobs->FindRecipeJob(Recipe->RecipeId, Recipe->SchemaVersion);
			Check(TimedJob.IsValid() && Jobs->CaptureJobs().Num() == 1 && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("Timed start commits input escrow without early output")); CheckProgress(3);
			Check(Player->AllowActorComponentToReplicate(Jobs) == COND_OwnerOnly,
				TEXT("The native jobs component joins the PlayerState owner-only scope"));
			if (Jobs->IsJobDue(TimedJob)) { Test->AddError(TEXT("Native early-collection probe missed its actual two-second pending window")); return true; }
			Jobs->RequestCollectJob(TimedJob);
			const auto EarlyCollect = Jobs->GetRequestState();
			Check(EarlyCollect.RequestId.IsValid() && EarlyCollect.JobId == TimedJob &&
				EarlyCollect.Action == EProject_JCraftingJobAction::Collect && EarlyCollect.Stage == EProject_JCraftingRequestStage::Rejected,
				TEXT("Fresh not-due collection returns a correlated synchronous rejection"));
			Check(!Profiles->IsCheckpointPending(Player) && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("A fresh early collect RPC is refused before native profile IO and grants nothing"));
			const FGuid UnknownJob = FGuid::NewGuid(); Jobs->RequestCancelJob(UnknownJob);
			const auto UnknownCancel = Jobs->GetRequestState();
			Check(UnknownCancel.RequestId.IsValid() && UnknownCancel.RequestId != EarlyCollect.RequestId && UnknownCancel.JobId == UnknownJob &&
				UnknownCancel.Action == EProject_JCraftingJobAction::Cancel && UnknownCancel.Stage == EProject_JCraftingRequestStage::Rejected,
				TEXT("Unknown job rejection uses a fresh correlation without altering an owned job"));
			Check(!Profiles->IsCheckpointPending(Player) && Jobs->CaptureJobs().Num() == 1,
				TEXT("An unknown owned-component RPC cannot cancel a different job"));
			ClientObservedJobs = Jobs;
			ClientRequestNotifyHandle = Jobs->OnRequestChanged.AddLambda([this]
			{
				if (const auto* Source = ClientObservedJobs.Get())
				{
					const auto State = Source->GetRequestState();
					if (State.Stage == EProject_JCraftingRequestStage::Pending) ++ClientPendingNotifications;
					if (State.Stage == EProject_JCraftingRequestStage::Confirmed) ++ClientConfirmedNotifications;
				}
			});
			Revision = Profiles->GetCommittedRevision(Player);
			Profiles->HoldCommandCompletionsForTest(true);
			ClientRequestObservedAt = FPlatformTime::Seconds();
			Jobs->RequestCancelJob(TimedJob);
			const auto Pending = Jobs->GetRequestState(); ClientCancelRequest = Pending.RequestId;
			Check(Pending.RequestId.IsValid() && Pending.RequestId != UnknownCancel.RequestId && Pending.JobId == TimedJob &&
				Pending.Action == EProject_JCraftingJobAction::Cancel && Pending.Stage == EProject_JCraftingRequestStage::Pending &&
				ClientPendingNotifications == 1 && ClientConfirmedNotifications == 0 && Profiles->IsCheckpointPending(Player),
				TEXT("Public cancel observes one real pending profile request before durable completion"));
			if (Pending.Stage != EProject_JCraftingRequestStage::Pending || !Profiles->IsCheckpointPending(Player))
			{ Test->AddError(TEXT("Client cancellation did not reach the actual pending profile route")); return true; }
			Jobs->RequestCollectJob(TimedJob);
			const auto Duplicate = Jobs->GetRequestState();
			Check(Duplicate.RequestId == ClientCancelRequest && Duplicate.JobId == TimedJob &&
				Duplicate.Action == EProject_JCraftingJobAction::Cancel && Duplicate.Stage == EProject_JCraftingRequestStage::Pending && ClientPendingNotifications == 1,
				TEXT("A pending local request blocks a second action without replacing its correlation"));
			Step = 1011; return false;
		}
		if (Step == 1011)
		{
			if (!Profiles->GetHeldCompletionCountForTest()) return false;
			auto* Jobs = Player->GetCraftingJobsComponent();
			const auto State = Jobs->GetRequestState();
			if (State.Stage == EProject_JCraftingRequestStage::Pending) return false;
			Check(State.Stage == EProject_JCraftingRequestStage::Unknown && State.RequestId == ClientCancelRequest &&
				State.JobId == TimedJob && State.Action == EProject_JCraftingJobAction::Cancel &&
				FPlatformTime::Seconds() - ClientRequestObservedAt >= 10.0 && ClientPendingNotifications == 1 && ClientConfirmedNotifications == 0 &&
				Jobs->CaptureJobs().Num() == 1 && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("Natural local response timeout is Unknown while real profile publication is held, not a claimed storage failure"));
			// This releases the already accepted native outcome; no success/clock/receipt is injected.
			Profiles->HoldCommandCompletionsForTest(false);
			Step = 102; return false;
		}
		if (Step == 102)
		{
			auto* Jobs = Player->GetCraftingJobsComponent(); const auto State = Jobs->GetRequestState();
			Check(State.Stage == EProject_JCraftingRequestStage::Confirmed && State.RequestId == ClientCancelRequest &&
				State.JobId == TimedJob && State.Action == EProject_JCraftingJobAction::Cancel &&
				ClientPendingNotifications == 1 && ClientConfirmedNotifications == 1 && Profiles->GetCommittedRevision(Player) == Revision + 1,
				TEXT("The real late native result confirms the same timed-out correlation once"));
			if (auto* Source = ClientObservedJobs.Get()) Source->OnRequestChanged.Remove(ClientRequestNotifyHandle);
			ClientObservedJobs.Reset();
			Check(Player->GetCraftingJobsComponent()->CaptureJobs().IsEmpty() && Count(Health) == 2 && Count(Mana) == 3,
				TEXT("Cancellation atomically refunds escrow and issues no output/progress")); CheckProgress(3);
			TimedStart = Prepare(EProject_JPersistentEdit::StartCraftJob); TimedJob = TimedStart.Request;
			Submit(TimedStart); Step = 103; return false;
		}
		if (Step == 103)
		{
			if (!Done) return false;
			const auto* Job = Player->GetCraftingJobsComponent()->FindJob(TimedJob);
			if (!Success || !First || !Job) { Test->AddError(TEXT("Second timed start did not durably publish its job")); return true; }
			TimedDue = Job->DueUtcTicks;
			TimedObservedJobs = Player->GetCraftingJobsComponent(); TimedJobPublications = 0;
			if (TimedObservedJobs->GetJobs().Num() != 1) { Test->AddError(TEXT("Published timed job lacks its single owner summary")); return true; }
			TimedNotifyHandle = TimedObservedJobs->OnChanged.AddLambda([this] { ++TimedJobPublications; });
			TimedSummaryRemaining = TimedObservedJobs->GetJobs()[0].RemainingSecondsAtPublish;
			Check(Count(Health) == 0 && Count(Mana) == 3, TEXT("Started job owns materials until collection/refund"));
			Submit(TimedStart); Step = 104; return false;
		}
		if (Step == 104)
		{
			if (!Done) return false;
			const auto* Job = Player->GetCraftingJobsComponent()->FindJob(TimedJob);
			Check(Success && !First && Job && Job->DueUtcTicks == TimedDue && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("Start receipt replay neither consumes materials twice nor extends the deadline"));
			auto Tracking = JobEdit(EProject_JPersistentEdit::SetQuestTracking, {}); Tracking.Quest = Quest->QuestId; Tracking.Amount = 0;
			Submit(Tracking); Step = 1041; return false;
		}
		if (Step == 1041)
		{
			if (!Done) return false;
			Check(Success && First && TimedJobPublications == 0 && TimedObservedJobs->GetJobs().Num() == 1 &&
				TimedObservedJobs->GetJobs()[0].RemainingSecondsAtPublish == TimedSummaryRemaining,
				TEXT("An unrelated durable quest edit does not republish or resample unchanged job summaries"));
			TimedObservedJobs->OnChanged.Remove(TimedNotifyHandle); TimedObservedJobs.Reset();
			TimedRetiredOwner = Player; Profiles->Detach(Player);
			Check(!Player->GetCraftingJobsComponent()->TryCancelJob(TimedJob), TEXT("Detached old session cannot refund its pending job"));
			auto* OldPawn = Player->GetPawn(); auto* OldController = OldPawn ? OldPawn->GetController() : nullptr;
			if (OldController) OldController->UnPossess();
			if (OldPawn) Check(OldPawn->Destroy(), TEXT("Old timed-work Avatar retires through its public lifecycle"));
			Check(Player->Destroy(), TEXT("Old timed-work PlayerState is actually destroyed before reopening"));
			if (OldController) OldController->Destroy();
			Player = World->SpawnActor<AProject_JPlayerState>();
			auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>(); Quests->EnsureDefinitionsResolved(); Quests->Definitions.Add(Quest.Get());
			Done = Success = false;
			Profiles->OpenProfile(Player, TEXT("CraftingExecution"), [this](bool OK) { Done = true; Success = OK; });
			Step = 105; return false;
		}
		if (Step == 105)
		{
			if (!Done) return false;
			const auto* Job = Player->GetCraftingJobsComponent()->FindJob(TimedJob);
			if (!Success || !Job) { Test->AddError(TEXT("Reconnect failed to restore the durable timed job")); return true; }
			Check(Job->DueUtcTicks == TimedDue && Count(Health) == 0 && Count(Mana) == 3,
				TEXT("Reconnect restores escrow and the original UTC deadline without auto-granting output")); CheckProgress(3);
			const auto FreshRequest = Player->GetCraftingJobsComponent()->GetRequestState();
			Check(FreshRequest.Stage == EProject_JCraftingRequestStage::Idle && !FreshRequest.RequestId.IsValid() && !FreshRequest.JobId.IsValid(),
				TEXT("A replacement PlayerState starts with fresh client request observation, independent of its restored durable jobs"));
			Check(!Profiles->SubmitEdit(TimedRetiredOwner.Get(), TimedStart) && !Profiles->SubmitEdit(Player, TimedStart),
				TEXT("Neither a retired owner nor a fresh session can authorize the old start request identity"));
			Check(ConfigureRecipe(2) && Profiles->IsReady(Player), TEXT("Removing the recipe cannot erase agreed job terms or quarantine its owner"));
			Step = 106; return false;
		}
		if (Step == 106)
		{
			if (!Player->GetCraftingJobsComponent()->IsJobDue(TimedJob)) return false;
			TimedCollect = JobEdit(EProject_JPersistentEdit::CollectCraftJob, TimedJob);
			Submit(TimedCollect); Step = 107; return false;
		}
		if (Step == 107)
		{
			if (!Done) return false;
			Check(Success && First && Player->GetCraftingJobsComponent()->CaptureJobs().IsEmpty() && Count(Health) == 0 && Count(Mana) == 4,
				TEXT("Natural server UTC collection grants frozen output and removes the job in one commit")); CheckProgress(4);
			Submit(TimedCollect); Step = 108; return false;
		}
		if (Step == 108)
		{
			if (!Done) return false;
			Check(Success && !First && Count(Health) == 0 && Count(Mana) == 4 && Player->GetCraftingJobsComponent()->CaptureJobs().IsEmpty(),
				TEXT("Collect receipt remains replayable after the job and recipe are absent, without another grant")); CheckProgress(4);
			Check(ConfigureRecipe(1), TEXT("Timed recipe can be restored for a normal bench collection"));
			Player->GetInventoryComponent()->AddItemDefinition(Health, 2, 1);
			Step = 109; return false;
		}
		if (Step == 109)
		{
			EnsureTimedAvatar();
			Check(Count(Health) == 2 && Station->TryCraft(Player->GetPawn()), TEXT("The current Avatar can start a fresh timed bench job"));
			Step = 110; return false;
		}
		if (Step == 110)
		{
			TimedJob = Player->GetCraftingJobsComponent()->FindRecipeJob(Recipe->RecipeId, Recipe->SchemaVersion);
			Check(TimedJob.IsValid() && Count(Health) == 0 && Count(Mana) == 4, TEXT("A second bench job still produces no early output")); CheckProgress(4);
			Step = 111; return false;
		}
		if (Step == 111)
		{
			if (!Player->GetCraftingJobsComponent()->IsJobDue(TimedJob)) return false;
			Check(ConfigureRecipe(2) && Station->TryCraft(Player->GetPawn()), TEXT("Actual bench collection resolves the saved job before a removed recipe"));
			Step = 112; return false;
		}
		if (Step == 112)
		{
			Check(Player->GetCraftingJobsComponent()->CaptureJobs().IsEmpty() && Count(Health) == 0 && Count(Mana) == 5,
				TEXT("Normal bench collection applies exactly one frozen output and one craft progression")); CheckProgress(5);
			Recipe->DurationSecondsPerBatch = 0; Check(ConfigureRecipe(1), TEXT("Original instant recipe restored before its existing IO-failure regression"));
			Player->GetInventoryComponent()->AddItemDefinition(Health, 2, 1);
			Step = 113; return false;
		}
		if (Step == 113)
		{
			Check(Count(Health) == 2 && Count(Mana) == 5, TEXT("Ordinary committed materials prepare the original valid write-failure command"));
			WriteFailureHealth = Count(Health); WriteFailureMana = Count(Mana);
			auto Failure = Prepare(); Profiles->FailCommandWritesForTest(Player); Submit(Failure);
			Step = 14; return false;
		}

		if (Step == 14)
		{
            CheckProgress(5);
			Check(Done && !Success && Count(Health) == WriteFailureHealth && Count(Mana) == WriteFailureMana,
				TEXT("Injected transaction write failure preserves both material and output ownership"));
			Test->AddInfo(FString::Printf(TEXT("Injected write failure: store=%d receipt=%d queried=%d outcome=%d calls=%d GT=%d"),
				int32(Probe->Value.StoreStatus), int32(Probe->Value.ReceiptStatus), Probe->Value.bReceiptQueried,
				int32(Probe->Value.Outcome), Probe->Calls, Probe->bGameThread));
			// The VFS fault may occur on an INSERT flush or on COMMIT. The latter
			// correctly preserves UnknownOutcome until an independent receipt read.
			Check(Probe->Calls == 1 && Probe->bGameThread &&
				(Probe->Value.StoreStatus == ProjectJ::MMO::ECommitStatus::Unavailable ||
				 Probe->Value.StoreStatus == ProjectJ::MMO::ECommitStatus::UnknownOutcome),
				TEXT("Write failure preserves the original unavailable/uncertain storage status"));
			Check(Probe->Value.bReceiptQueried && Probe->Value.ReceiptStatus == ProjectJ::MMO::ECommitStatus::Invalid &&
				Probe->Value.Outcome == EProject_JProfileDurableOutcome::ProvenAbsent,
				TEXT("Only an independent exact-session receipt absence proves this failed request absent"));
			Check(RetiredProbe->Calls == 1, TEXT("Owner-independent accepted job does not deliver twice"));
			Check(ProgressionPublications == 0, TEXT("Crafting reuses inventory publication without unrelated progression broadcasts"));
			Check(!WorkerProbe->WrongThread.load(std::memory_order_relaxed) && !WorkerProbe->TimedOut.load(std::memory_order_relaxed),
				TEXT("Success, capacity and missing-material plans retain the same worker execution contract"));
			return true;
		}
		return false;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCraftingExecutionTest, "ProjectJ.MMO.Crafting.AtomicInteractionAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCraftingExecutionTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World = GI->GetWorld(); bool Queued = false;
	ON_SCOPE_EXIT {
		if (!Queued) {
			if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		}
	};
	auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
	auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
	if (!TestTrue(TEXT("Actual canonical consumables load"), Health && Mana && Data->LoadPrimaryAssetDefinitions())) return false;
	auto* Recipe = NewObject<UProject_JCraftingRecipeDefinition>(Data);
	Recipe->RecipeId = TEXT("OwnedCraftingTest"); Recipe->MaxBatches = 20;
	FProject_JCraftingRecipeLine Input; Input.Definition = Health; Input.CountPerBatch = 2;
	FProject_JCraftingRecipeLine Output; Output.Definition = Mana;
	Recipe->Inputs.Add(Input); Recipe->Outputs.Add(Output); Data->CraftingRecipeDefinitions.Add(Recipe);
	ProjectJ::Crafting::FRecipeValues Values; FString Rules;
	if (!TestTrue(TEXT("Recipe captures canonical IDs from actual items"), Recipe->Capture(*Data, Values, Rules))) return false;

	const FString InstantDigest = Rules;
	Recipe->DurationSecondsPerBatch = 1;
	ProjectJ::Crafting::FRecipeValues TimedCaptured; FString TimedDigest;
	if (!TestTrue(TEXT("Positive duration is captured and domain-separated from the original instant digest"),
		Recipe->Capture(*Data, TimedCaptured, TimedDigest) && TimedCaptured.DurationSecondsPerBatch == 1 && TimedDigest != InstantDigest)) return false;
	Recipe->DurationSecondsPerBatch = 0;
	if (!TestTrue(TEXT("Returning to duration zero preserves the exact original instant recipe digest"),
		Recipe->Capture(*Data, TimedCaptured, TimedDigest) && TimedCaptured.DurationSecondsPerBatch == 0 && TimedDigest == InstantDigest)) return false;

	// Coarse pure workload covers capacity, locks and cross-input atomicity before native IO.
	FProject_JLocalProfileSnapshot Before, After;
	FProject_JPersistentItem Material; Material.Instance = FGuid::NewGuid(); Material.Definition = Health->ItemId;
	Material.DefinitionVersion = Health->SchemaVersion; Material.Count = 2; Material.Order = 0; Before.Items.Add(Material);
	using namespace ProjectJ::Crafting;
	if (!TestTrue(TEXT("Normal plan consumes and produces a complete value snapshot"), Plan(Values, 1, {}, Before, After) == EPlanResult::Success &&
		After.Items.Num() == 1 && After.Items[0].Definition == Mana->ItemId && After.Items[0].Count == 1)) return false;
	const auto Sentinel = After.Items[0].Instance;
	TestTrue(TEXT("Locked material cannot be consumed and leaves result untouched"), Plan(Values, 1, {Material.Instance}, Before, After) == EPlanResult::LockedMaterials && After.Items[0].Instance == Sentinel);
	Before.Items[0].Slot = EProject_JEquipmentSlot::Weapon;
	TestTrue(TEXT("Equipped material is not an available input"), Plan(Values, 1, {}, Before, After) == EPlanResult::LockedMaterials);
	Before.Items[0].Slot = EProject_JEquipmentSlot::None;
	auto Multi = Values; Multi.Inputs.Add(Values.Outputs[0]);
	TestTrue(TEXT("Own outputs cannot satisfy a missing input of the same command"), Plan(Multi, 1, {}, Before, After) == EPlanResult::MissingMaterials && After.Items[0].Instance == Sentinel);
	Before.Items[0].Count = 3;
	for (int32 Index = 1; Index < 4096; ++Index) { auto Filler = Material; Filler.Instance = FGuid::NewGuid(); Filler.Definition = TEXT("CapacityFixture"); Filler.Order = Index; Before.Items.Add(Filler); }
	auto Full = Values; Full.Outputs[0].MaxStackCount = 1; Full.Outputs[0].CountPerBatch = 2;
	TestTrue(TEXT("Final capacity is checked before output allocation and preserves all input on failure"), Plan(Full, 1, {}, Before, After) == EPlanResult::BagFull && Before.Items[0].Count == 3 && After.Items[0].Instance == Sentinel);
	// The quest grant consumer uses this same packer without inventing an input recipe.
	{
		FProject_JLocalProfileSnapshot GrantBefore, Granted;
		GrantBefore.Items.Add(Material);
		auto Reward = Values.Inputs[0];
		if (!TestTrue(TEXT("Canonical stack supports the grant merge control"), Reward.MaxStackCount > 2 && Reward.MaxStackCount <= 1000000)) return false;
		Reward.CountPerBatch = Reward.MaxStackCount - 1;
		if (!TestTrue(TEXT("Grant keeps the old identity and packs only the remainder into a new stack"),
			PlanGrant({Reward}, {}, GrantBefore, Granted) == EPlanResult::Success && Granted.Items.Num() == 2 &&
			Granted.Items[0].Instance == Material.Instance && Granted.Items[0].Count == Reward.MaxStackCount &&
			Granted.Items[1].Instance != Material.Instance && Granted.Items[1].Count == 1 && GrantBefore.Items[0].Count == 2)) return false;
		if (!TestTrue(TEXT("A locked bag stack is not a grant merge target"),
			PlanGrant({Reward}, {Material.Instance}, GrantBefore, Granted) == EPlanResult::Success &&
			Granted.Items[0].Count == 2 && Granted.Items[1].Count == Reward.CountPerBatch)) return false;
		const FGuid GrantSentinel = Granted.Items[1].Instance;
		const int32 GrantSentinelCount = Granted.Items[1].Count;
		TestTrue(TEXT("A full bag rejects the whole reward and preserves the previous result"),
			PlanGrant(Values.Outputs, {}, Before, Granted) == EPlanResult::BagFull && Granted.Items.Num() == 2 &&
			Granted.Items[1].Instance == GrantSentinel && Granted.Items[1].Count == GrantSentinelCount);
		TestTrue(TEXT("Empty or duplicate reward lines cannot become a partial grant"),
			PlanGrant({}, {}, GrantBefore, Granted) == EPlanResult::InvalidRecipe &&
			PlanGrant({Reward, Reward}, {}, GrantBefore, Granted) == EPlanResult::InvalidRecipe && Granted.Items[1].Instance == GrantSentinel);
		auto OrderFull = GrantBefore; OrderFull.Items[0].Order = MAX_int32 - 1;
		TestTrue(TEXT("Grant order exhaustion rejects before publishing a new instance"),
			PlanGrant(Values.Outputs, {}, OrderFull, Granted) == EPlanResult::BagFull && Granted.Items[1].Instance == GrantSentinel);
	}
	//112: pure value/time inputs only. Keep the existing instant/native fixture untouched.
	{
		auto Timed = Values; Timed.DurationSecondsPerBatch = 2;
		const int64 Now = FDateTime(2026, 11, 2).GetTicks();
		FProject_JLocalProfileSnapshot JobBefore; JobBefore.Format = 2;
		JobBefore.Items.Add(Material);
		FProject_JQuestState QuestState; QuestState.QuestId = TEXT("PureTimedCraftProgress"); QuestState.Count = 3;
		JobBefore.Quests.States.Add(QuestState);
		const auto SameItem = [](const auto& A, const auto& B)
		{
			return A.Instance == B.Instance && A.Definition == B.Definition && A.DefinitionVersion == B.DefinitionVersion &&
				A.Level == B.Level && A.Count == B.Count && A.Order == B.Order && A.Slot == B.Slot && A.bInBag == B.bInBag;
		};
		const auto SameSnapshot = [&](const FProject_JLocalProfileSnapshot& A, const FProject_JLocalProfileSnapshot& B)
		{
			if (A.Format != B.Format || A.Account != B.Account || A.Character != B.Character || A.RulesFingerprint != B.RulesFingerprint ||
				A.Quests.Version != B.Quests.Version || A.Quests.States != B.Quests.States || A.Items.Num() != B.Items.Num() ||
				A.CraftingJobs.Num() != B.CraftingJobs.Num() || A.Progression.SchemaVersion != B.Progression.SchemaVersion ||
				A.Progression.Experience != B.Progression.Experience || A.Progression.ClassId != B.Progression.ClassId ||
				A.Progression.Level != B.Progression.Level || A.Progression.AdvancementHistory != B.Progression.AdvancementHistory) return false;
			for (int32 I = 0; I < A.Items.Num(); ++I) if (!SameItem(A.Items[I], B.Items[I])) return false;
			for (int32 I = 0; I < A.CraftingJobs.Num(); ++I)
			{
				const auto& L = A.CraftingJobs[I]; const auto& R = B.CraftingJobs[I];
				if (L.SchemaVersion != R.SchemaVersion || L.Id != R.Id || L.RecipeId != R.RecipeId || L.RecipeVersion != R.RecipeVersion ||
					L.Batches != R.Batches || L.RulesDigest != R.RulesDigest || L.StartedUtcTicks != R.StartedUtcTicks ||
					L.DueUtcTicks != R.DueUtcTicks || L.Escrow.Num() != R.Escrow.Num() || L.Outputs.Num() != R.Outputs.Num()) return false;
				for (int32 J = 0; J < L.Escrow.Num(); ++J) if (!SameItem(L.Escrow[J], R.Escrow[J])) return false;
				for (int32 J = 0; J < L.Outputs.Num(); ++J)
				{
					const auto& X = L.Outputs[J]; const auto& Y = R.Outputs[J];
					if (X.Definition != Y.Definition || X.DefinitionVersion != Y.DefinitionVersion || X.Level != Y.Level ||
						X.CountPerBatch != Y.CountPerBatch || X.MaxStackCount != Y.MaxStackCount) return false;
				}
			}
			return true;
		};
		const auto Count = [](const auto& Snapshot, FName Definition)
		{
			int64 Units = 0; for (const auto& Item : Snapshot.Items) if (Item.Definition == Definition) Units += Item.Count;
			return Units;
		};
		// Use a nontrivial existing result as failure sentinel; compare all owned fields.
		const auto Unchanged = After;
		auto RejectedOut = Unchanged;
		const auto WholeId = FGuid::NewGuid();
		FProject_JLocalProfileSnapshot Whole;
		if (!TestTrue(TEXT("Timed start fully escrows the original identity and explicitly promotes legacy Format2"),
			StartJob(Timed, 1, Rules, WholeId, Now, {}, JobBefore, Whole) == EPlanResult::Success &&
			Whole.Format == 3 && Whole.Items.IsEmpty() && Whole.CraftingJobs.Num() == 1 &&
			Whole.CraftingJobs[0].Id == WholeId && Whole.CraftingJobs[0].SchemaVersion == 1 &&
			Whole.CraftingJobs[0].Escrow.Num() == 1 && SameItem(Whole.CraftingJobs[0].Escrow[0], Material))) return false;

		auto LegacyNoOwnership = JobBefore; LegacyNoOwnership.Format = 1;
		TestTrue(TEXT("Timed planning cannot turn an unmigrated Format1 into owned escrow"),
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), Now, {}, LegacyNoOwnership, RejectedOut) == EPlanResult::InvalidInventory && SameSnapshot(RejectedOut, Unchanged));
		auto LegacyWithJobs = Whole; LegacyWithJobs.Format = 2;
		TestTrue(TEXT("A legacy format cannot silently carry or collect newer job ownership"),
			CollectJob(WholeId, Whole.CraftingJobs[0].DueUtcTicks, {}, LegacyWithJobs, RejectedOut) == EPlanResult::InvalidInventory && SameSnapshot(RejectedOut, Unchanged));
		const auto Due = Whole.CraftingJobs[0].DueUtcTicks;
		TestTrue(TEXT("Start grants no output or quest progress and does not mutate its input"),
			Count(Whole, Mana->ItemId) == 0 && Whole.Quests.States == JobBefore.Quests.States && Whole.Quests.Version == JobBefore.Quests.Version &&
			JobBefore.Format == 2 && JobBefore.Items.Num() == 1 && SameItem(JobBefore.Items[0], Material) && JobBefore.CraftingJobs.IsEmpty() &&
			Due == Now + 2 * ETimespan::TicksPerSecond && Whole.CraftingJobs[0].RulesDigest == Rules);
		TestTrue(TEXT("Positive-duration recipe cannot bypass escrow through instant Plan"),
			Plan(Timed, 1, {}, JobBefore, RejectedOut) == EPlanResult::InvalidRecipe && SameSnapshot(RejectedOut, Unchanged));
		TestTrue(TEXT("Collection one tick before due is atomic NotReady"),
			CollectJob(WholeId, Due - 1, {}, Whole, RejectedOut) == EPlanResult::NotReady && SameSnapshot(RejectedOut, Unchanged));
		// The saved job, not a subsequently edited recipe value, supplies owed units.
		auto EditedRecipe = Timed; EditedRecipe.Outputs[0].CountPerBatch += 7;
		auto WithOtherJob = Whole; auto OtherJob = Whole.CraftingJobs[0];
		OtherJob.Id = FGuid::NewGuid(); OtherJob.Escrow[0].Instance = FGuid::NewGuid(); WithOtherJob.CraftingJobs.Add(OtherJob);
		FProject_JLocalProfileSnapshot Collected;
		if (!TestTrue(TEXT("Exact due collection grants frozen units and removes only the saved job"),
			CollectJob(WholeId, Due, {}, WithOtherJob, Collected) == EPlanResult::Success && Collected.CraftingJobs.Num() == 1 &&
			Collected.CraftingJobs[0].Id == OtherJob.Id && Collected.CraftingJobs[0].Escrow.Num() == 1 &&
			SameItem(Collected.CraftingJobs[0].Escrow[0], OtherJob.Escrow[0]) &&
			Collected.Items.Num() == 1 && Collected.Items[0].DefinitionVersion == Whole.CraftingJobs[0].Outputs[0].DefinitionVersion &&
			Collected.Items[0].Level == Whole.CraftingJobs[0].Outputs[0].Level &&
			Count(Collected, Mana->ItemId) == Values.Outputs[0].CountPerBatch && Count(Collected, Mana->ItemId) != EditedRecipe.Outputs[0].CountPerBatch &&
			Collected.Quests.States == JobBefore.Quests.States && Collected.Format == Whole.Format)) return false;
		TestTrue(TEXT("Terminal collection cannot mint a second result"),
			CollectJob(WholeId, Due, {}, Collected, RejectedOut) == EPlanResult::JobNotFound && SameSnapshot(RejectedOut, Unchanged));
		FProject_JLocalProfileSnapshot Refunded;
		if (!TestTrue(TEXT("Cancel restores a whole escrow identity without a current recipe"),
			CancelJob(WholeId, {}, Whole, Refunded) == EPlanResult::Success && Refunded.CraftingJobs.IsEmpty() &&
			Refunded.Items.Num() == 1 && SameItem(Refunded.Items[0], Material) && Refunded.Quests.States == JobBefore.Quests.States && Refunded.Format == 3)) return false;
		TestTrue(TEXT("Terminal cancellation cannot refund twice"),
			CancelJob(WholeId, {}, Refunded, RejectedOut) == EPlanResult::JobNotFound && SameSnapshot(RejectedOut, Unchanged));
		auto PagedBefore = JobBefore; PagedBefore.Format = 4;
		ProjectJ::QuestHistory::FViewPtr History;
		FString HistoryFailure;
		if (!TestTrue(TEXT("A completed quest supplies a real immutable archive before crafting"),
			ProjectJ::QuestHistory::Append({}, TEXT("CompletedBeforeCraft"), History, HistoryFailure))) return false;
		PagedBefore.Quests.Version = 3; PagedBefore.Quests.History = History;
		PagedBefore.Quests.HistoryCount = 1; PagedBefore.Quests.HistoryRootDigest = ProjectJ::QuestHistory::RootDigest(History);
		FProject_JLocalProfileSnapshot PagedWhole, PagedRefunded;
		const FGuid PagedJobId = FGuid::NewGuid();
		if (!TestTrue(TEXT("Starting a timed job preserves Format4 and shares its completed-history pages"),
			StartJob(Timed, 1, Rules, PagedJobId, Now, {}, PagedBefore, PagedWhole) == EPlanResult::Success &&
			PagedWhole.Format == 4 && PagedWhole.Quests.History == History && PagedWhole.Quests.HistoryCount == 1)) return false;
		TestTrue(TEXT("Cancelling a timed job restores escrow while retaining the same completed-history value"),
			CancelJob(PagedJobId, {}, PagedWhole, PagedRefunded) == EPlanResult::Success &&
			PagedRefunded.Format == 4 && PagedRefunded.Quests.History == History && PagedRefunded.Quests.HistoryCount == 1 &&
			PagedRefunded.Items.Num() == 1 && SameItem(PagedRefunded.Items[0], Material));
		auto UnhydratedBefore = PagedBefore; UnhydratedBefore.Quests.History.Reset();
		TestTrue(TEXT("A metadata-only profile cannot enter a crafting mutation before its history has loaded"),
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), Now, {}, UnhydratedBefore, RejectedOut) == EPlanResult::InvalidInventory &&
			SameSnapshot(RejectedOut, Unchanged));
		auto PartialBefore = JobBefore; PartialBefore.Items[0].Count = 3;
		FProject_JLocalProfileSnapshot Partial, PartialRefund;
		const auto PartialId = FGuid::NewGuid();
		if (!TestTrue(TEXT("Partial consumption owns a distinct escrow UUID and preserves the live remainder"),
			StartJob(Timed, 1, Rules, PartialId, Now, {}, PartialBefore, Partial) == EPlanResult::Success &&
			Partial.Items.Num() == 1 && Partial.Items[0].Instance == Material.Instance && Partial.Items[0].Count == 1 &&
			Partial.CraftingJobs.Num() == 1 && Partial.CraftingJobs[0].Escrow.Num() == 1 &&
			Partial.CraftingJobs[0].Escrow[0].Instance.IsValid() && Partial.CraftingJobs[0].Escrow[0].Instance != Material.Instance &&
			Partial.CraftingJobs[0].Escrow[0].Count == 2 && ValidateJobState(Partial.Items, Partial.CraftingJobs))) return false;
		const auto SplitId = Partial.CraftingJobs[0].Escrow[0].Instance;
		TestTrue(TEXT("Cancel returns the exact split identity and never merges into a locked surviving stack"),
			CancelJob(PartialId, {Material.Instance}, Partial, PartialRefund) == EPlanResult::Success &&
			PartialRefund.CraftingJobs.IsEmpty() && PartialRefund.Items.Num() == 2 && Count(PartialRefund, Health->ItemId) == 3 &&
			PartialRefund.Items[0].Instance == Material.Instance && PartialRefund.Items[0].Count == 1 &&
			PartialRefund.Items[1].Instance == SplitId && PartialRefund.Items[1].Count == 2 && PartialRefund.Items[1].Order != PartialRefund.Items[0].Order);
		auto Eight = Whole;
		for (int32 I = 1; I < MaxJobs; ++I)
		{
			auto Job = Whole.CraftingJobs[0]; Job.Id = FGuid::NewGuid(); Job.Escrow[0].Instance = FGuid::NewGuid(); Eight.CraftingJobs.Add(MoveTemp(Job));
		}
		auto NewMaterial = Material; NewMaterial.Instance = FGuid::NewGuid(); Eight.Items.Add(NewMaterial);
		TestTrue(TEXT("Eight existing jobs reject a ninth without partial escrow or output mutation"),
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), Now, {}, Eight, RejectedOut) == EPlanResult::JobCapacity && SameSnapshot(RejectedOut, Unchanged));
		auto FragmentsRecipe = Timed; FragmentsRecipe.Inputs[0].CountPerBatch = MaxEscrowEntries; FragmentsRecipe.Inputs[0].MaxStackCount = 1;
		auto Fragments = JobBefore; Fragments.Items.Reset();
		for (int32 I = 0; I < MaxEscrowEntries; ++I)
		{
			auto Item = Material; Item.Instance = FGuid::NewGuid(); Item.Count = 1; Item.Order = I; Fragments.Items.Add(MoveTemp(Item));
		}
		FProject_JLocalProfileSnapshot SixtyFour;
		if (!TestTrue(TEXT("Exactly64 escrow rows are supported"),
			StartJob(FragmentsRecipe, 1, Rules, FGuid::NewGuid(), Now, {}, Fragments, SixtyFour) == EPlanResult::Success &&
			SixtyFour.Items.IsEmpty() && SixtyFour.CraftingJobs.Num() == 1 && SixtyFour.CraftingJobs[0].Escrow.Num() == MaxEscrowEntries)) return false;
		FragmentsRecipe.Inputs[0].CountPerBatch += 1;
		auto LastFragment = Material; LastFragment.Instance = FGuid::NewGuid(); LastFragment.Count = 1; LastFragment.Order = MaxEscrowEntries; Fragments.Items.Add(LastFragment);
		TestTrue(TEXT("A65th escrow row fails atomically instead of exceeding job storage"),
			StartJob(FragmentsRecipe, 1, Rules, FGuid::NewGuid(), Now, {}, Fragments, RejectedOut) == EPlanResult::JobCapacity && SameSnapshot(RejectedOut, Unchanged));
		// Reuse the existing4096-row workload instead of building a larger fixture.
		auto OwnedFull = Before; OwnedFull.CraftingJobs.Reset(); OwnedFull.Items[0].Count = 3;
		TestTrue(TEXT("A partial escrow split counts against combined4096 ownership"),
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), Now, {}, OwnedFull, RejectedOut) == EPlanResult::BagFull && SameSnapshot(RejectedOut, Unchanged));
		OwnedFull.Items[0].Count = 2;
		auto TwoOutputRecipe = Timed; TwoOutputRecipe.Outputs[0].CountPerBatch = 2; TwoOutputRecipe.Outputs[0].MaxStackCount = 1;
		const auto FullJobId = FGuid::NewGuid(); FProject_JLocalProfileSnapshot FullJob, FullClaim;
		if (!TestTrue(TEXT("A whole escrow transfer fits combined4096 ownership without inventing a free slot"),
			StartJob(TwoOutputRecipe, 1, Rules, FullJobId, Now, {}, OwnedFull, FullJob) == EPlanResult::Success &&
			FullJob.Items.Num() == MaxOwnedEntries - 1 && FullJob.CraftingJobs.Num() == 1 && ValidateJobState(FullJob.Items, FullJob.CraftingJobs))) return false;
		TestTrue(TEXT("Full bag defers frozen output collection with the entire job retained"),
			CollectJob(FullJobId, Due, {}, FullJob, RejectedOut) == EPlanResult::BagFull && SameSnapshot(RejectedOut, Unchanged) &&
			FullJob.CraftingJobs.Num() == 1 && FullJob.CraftingJobs[0].Escrow[0].Instance == Material.Instance);
		FullJob.Items.RemoveAt(FullJob.Items.Num() - 1);
		TestTrue(TEXT("Space released later allows the same frozen job to collect exactly once"),
			CollectJob(FullJobId, Due, {}, FullJob, FullClaim) == EPlanResult::Success && FullClaim.CraftingJobs.IsEmpty() &&
			FullClaim.Items.Num() == MaxOwnedEntries && Count(FullClaim, Mana->ItemId) == 2);
		auto OrderBlocked = SixtyFour; const auto OrderJobId = OrderBlocked.CraftingJobs[0].Id;
		const auto OrderEscrowId = OrderBlocked.CraftingJobs[0].Escrow[0].Instance;
		auto OrderItem = Material; OrderItem.Instance = FGuid::NewGuid();
		OrderItem.Definition = TEXT("RefundOrderFixture"); OrderItem.Order = MAX_int32 - MaxEscrowEntries; OrderBlocked.Items.Add(OrderItem);
		TestTrue(TEXT("Refund order exhaustion retains escrow and leaves output unchanged"),
			CancelJob(OrderJobId, {}, OrderBlocked, RejectedOut) == EPlanResult::BagFull && SameSnapshot(RejectedOut, Unchanged) && OrderBlocked.CraftingJobs.Num() == 1);
		OrderBlocked.Items[0].Order = 0;
		TestTrue(TEXT("Refund retries after bag order capacity is available"),
			CancelJob(OrderJobId, {}, OrderBlocked, Refunded) == EPlanResult::Success && Refunded.CraftingJobs.IsEmpty() &&
			Refunded.Items.Num() == MaxEscrowEntries + 1 && Refunded.Items[1].Instance == OrderEscrowId &&
			Refunded.Items[1].Order == 1 && Refunded.Items.Last().Order == MaxEscrowEntries);
		auto BadIdentity = Partial; BadIdentity.CraftingJobs[0].Escrow[0].Instance = BadIdentity.Items[0].Instance;
		TestTrue(TEXT("Cross live and escrow UUID aliasing is rejected atomically"),
			CancelJob(PartialId, {}, BadIdentity, RejectedOut) == EPlanResult::InvalidInventory && SameSnapshot(RejectedOut, Unchanged));
		TestTrue(TEXT("Negative pure UTC and DateTime deadline overflow fail without mutation"),
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), -1, {}, JobBefore, RejectedOut) == EPlanResult::InvalidTime &&
			StartJob(Timed, 1, Rules, FGuid::NewGuid(), FDateTime::MaxValue().GetTicks() - ETimespan::TicksPerSecond, {}, JobBefore, RejectedOut) == EPlanResult::InvalidTime &&
			SameSnapshot(RejectedOut, Unchanged));
		auto LongRecipe = Timed; LongRecipe.DurationSecondsPerBatch = 86400; LongRecipe.MaxBatches = 8;
		TestTrue(TEXT("More than seven days per job is rejected before consumption"),
			StartJob(LongRecipe, 8, Rules, FGuid::NewGuid(), Now, {}, JobBefore, RejectedOut) == EPlanResult::InvalidTime && SameSnapshot(RejectedOut, Unchanged));
		auto WeekBefore = JobBefore; WeekBefore.Items[0].Count = 14; LongRecipe.Inputs[0].MaxStackCount = 14;
		FProject_JLocalProfileSnapshot Week;
		TestTrue(TEXT("Exactly seven days has a representable frozen deadline"),
			StartJob(LongRecipe, 7, Rules, FGuid::NewGuid(), Now, {}, WeekBefore, Week) == EPlanResult::Success &&
			Week.CraftingJobs.Num() == 1 && Week.CraftingJobs[0].DueUtcTicks - Week.CraftingJobs[0].StartedUtcTicks == int64(MaxJobSeconds) * ETimespan::TicksPerSecond);
	}

	//113: synthetic presentation values, never an owned asset grant or durable commit.
	// Insert before auto Configure, beside the pure planner block; use an isolated native PS.
	{
		const TStrongObjectPtr<AProject_JPlayerState> ProjectionOwner(World->SpawnActor<AProject_JPlayerState>());
		if (!TestNotNull(TEXT("Isolated generation probe uses a real native PlayerState"), ProjectionOwner.Get())) return false;
		ON_SCOPE_EXIT { if (ProjectionOwner.IsValid() && !ProjectionOwner->IsActorBeingDestroyed()) ProjectionOwner->Destroy(); };
		auto* ProjectionJobs = ProjectionOwner->GetCraftingJobsComponent();
		if (!TestNotNull(TEXT("Isolated native PS owns the production jobs component"), ProjectionJobs)) return false;
		FProject_JAccountId ProbeAccount; ProbeAccount.Value = FGuid::NewGuid();
		FProject_JCharacterId CharacterA, CharacterB; CharacterA.Value = FGuid::NewGuid(); CharacterB.Value = FGuid::NewGuid();
		ProjectionOwner->SetIdentity(ProbeAccount, CharacterA);
		ProjectJ::Crafting::FJobValues ProjectionJob;
		ProjectionJob.Id = FGuid::NewGuid(); ProjectionJob.RecipeId = Values.RecipeId; ProjectionJob.RecipeVersion = Values.SchemaVersion;
		ProjectionJob.RulesDigest = Rules; ProjectionJob.StartedUtcTicks = FDateTime::UtcNow().GetTicks();
		ProjectionJob.DueUtcTicks = ProjectionJob.StartedUtcTicks + 2 * ETimespan::TicksPerSecond;
		ProjectionJob.Escrow.Add(Material); ProjectionJob.Outputs = Values.Outputs;
		if (!TestTrue(TEXT("Explicit synthetic job payload passes structural admission without profile IO"),
			ProjectionJobs->ValidateCommittedJobs({ProjectionJob}))) return false;
		ProjectionJobs->InstallCommittedJobs({ProjectionJob});
		const uint64 GenerationA = ProjectionJobs->GetPresentationGeneration();
		const auto OldRows = ProjectionJobs->GetJobs();
		if (!TestTrue(TEXT("Initial synthetic row is stamped with the installed native character generation"),
			GenerationA != 0 && OldRows.Num() == 1 && OldRows[0].JobId == ProjectionJob.Id &&
			OldRows[0].OwnerCharacterId == CharacterA.Value && OldRows[0].OwnerGeneration == GenerationA && ProjectionJobs->FindJob(ProjectionJob.Id))) return false;
		ProjectionJobs->NotifyOwnerIdentity(CharacterA.Value);
		TestTrue(TEXT("Repeated same identity notification preserves an installed presentation scope"),
			ProjectionJobs->GetPresentationGeneration() == GenerationA && ProjectionJobs->GetJobs() == OldRows);
		ProjectionOwner->SetIdentity(ProbeAccount, CharacterB);
		const uint64 GenerationB = ProjectionJobs->GetPresentationGeneration();
		TestTrue(TEXT("Different native identity retires old rows and actions before another projection is installed"),
			GenerationB > GenerationA && ProjectionJobs->GetJobs().IsEmpty() && !ProjectionJobs->FindJob(ProjectionJob.Id) &&
			ProjectionJobs->FindRecipeJob(Values.RecipeId, Values.SchemaVersion).IsValid() == false && ProjectionJobs->GetRemainingSeconds(ProjectionJob.Id) < 0);
		ProjectionOwner->SetIdentity(ProbeAccount, CharacterA);
		const uint64 ReturnedGenerationA = ProjectionJobs->GetPresentationGeneration();
		TestTrue(TEXT("A to B to A value return cannot resurrect an old installed row or job action"),
			ReturnedGenerationA > GenerationB && ProjectionJobs->GetJobs().IsEmpty() && !ProjectionJobs->FindJob(ProjectionJob.Id) &&
			ProjectionJobs->GetRemainingSeconds(ProjectionJob.Id) < 0 && ProjectionJobs->GetRequestState().Stage == EProject_JCraftingRequestStage::Idle);
		ProjectionJobs->InstallCommittedJobs({ProjectionJob});
		const auto FreshRows = ProjectionJobs->GetJobs();
		if (!TestTrue(TEXT("Only an explicit fresh install makes the same synthetic job visible in the returned identity"),
			FreshRows.Num() == 1 && FreshRows[0].JobId == OldRows[0].JobId && FreshRows[0].OwnerCharacterId == OldRows[0].OwnerCharacterId &&
			FreshRows[0].OwnerGeneration == ReturnedGenerationA && ProjectionJobs->FindJob(ProjectionJob.Id))) return false;
		TestFalse(TEXT("Captured old row context cannot match a fresh generation even when PS character and job IDs are equal"),
			FreshRows.ContainsByPredicate([&](const FProject_JCraftingJobSummary& Row)
			{
				return Row.JobId == OldRows[0].JobId && Row.OwnerCharacterId == OldRows[0].OwnerCharacterId && Row.OwnerGeneration == OldRows[0].OwnerGeneration;
			}));
	}

	auto Configure = [Data, Recipe](int32 Mode) {
		Data->CraftingRecipeDefinitions.RemoveAll([Recipe](const auto& Soft) { return Soft.Get() == Recipe; });
		Recipe->MaxBatches = Mode == 0 ? 0 : 20;
		if (Mode != 2) Data->CraftingRecipeDefinitions.Add(Recipe);
		return Data->RefreshCraftingRecipes();
	};
	TestFalse(TEXT("Invalid optional recipe is refused independently"), Configure(0));
	TestTrue(TEXT("Optional recipe failure cannot block core item/class publication"), Data->LoadPrimaryAssetDefinitions() && Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health);
	auto* Player = World->SpawnActor<AProject_JPlayerState>();
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/CraftingExecution") / FGuid::NewGuid().ToString() / TEXT("Profiles.sqlite"));
	GI->GetSubsystem<UProject_JLocalProfileSubsystem>()->DatabasePath = File;
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
	// No profile readiness or PlayerState is required merely to display a prompt.
	auto* AdvisoryPawn = World->SpawnActor<APawn>(); auto* AdvisoryBench = World->SpawnActor<AProject_JCraftingStation>();
	AdvisoryBench->RecipeId = Recipe->RecipeId;
	TestTrue(TEXT("Dynamic bench actor participates in replication"), AdvisoryBench->GetIsReplicated());
	for (FName PropertyName : {FName(TEXT("RecipeId")), FName(TEXT("RecipeVersion")), FName(TEXT("Batches"))})
	{
		const auto* Property = FindFProperty<FProperty>(AProject_JCraftingStation::StaticClass(), PropertyName);
		TestTrue(TEXT("Bench configuration is a reflected replicated property"), Property && Property->HasAnyPropertyFlags(CPF_Net));
	}
	TestTrue(TEXT("Advisory interaction does not consult server-only local-profile readiness"), AdvisoryBench->CanInteract_Implementation(AdvisoryPawn));
	TestFalse(TEXT("Actual craft requires a current admitted PlayerState"), AdvisoryBench->TryCraft(AdvisoryPawn));
	AdvisoryBench->Destroy(); AdvisoryPawn->Destroy();
	ADD_LATENT_AUTOMATION_COMMAND(FCraftingExecution(this, GI, Player, Recipe, Health, Mana, MoveTemp(Configure)));
	Queued = true;
	return true;
}
#endif
