// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameMode.h"
#include "Engine/World.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "Project_JPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameSession.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/Project_JIpNetDriver.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Backend/Project_JServerDrainSubsystem.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#include "Backend/Project_JOnlineCrafting.h"
#include "Backend/Project_JOnlineQuests.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Dom/JsonObject.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Social/Project_JSocialSubsystem.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Engine/GameInstance.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Pawn.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class FProject_JBackendIntegrityBridge final : public IProject_JAntiCheatBridge
{
public:
	explicit FProject_JBackendIntegrityBridge(UWorld* InWorld) : Origin(InWorld) {}
	bool IsClientAttested(const FGuid&) const override { return false; }
	void ReportServerViolation(const FGuid& Character, FName Code, int32 Severity) override
	{
		UWorld* World = Origin.Get();
		if (!World || World->bIsTearingDown || !UProject_JOnlineWorldSubsystem::UsesOnlineBackend(World)) return;
		auto Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
		Body->SetStringField(TEXT("violation_code"), Code.ToString());
		Body->SetNumberField(TEXT("severity"), Severity);
		Body->SetStringField(TEXT("zone"), World->GetMapName());
		UProject_JOnlineWorldSubsystem::Submit(World, TEXT("Security"), TEXT("Report_Violation"), Body, FGuid::NewGuid(), [](auto) {});
	}
private:
	TWeakObjectPtr<UWorld> Origin;
};
}

AProject_JGameMode::AProject_JGameMode()
{
	GameStateClass = AProject_JGameState::StaticClass();
	PlayerStateClass = AProject_JPlayerState::StaticClass();
}

void AProject_JGameMode::InitGameState()
{
	const bool bOnline = UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld());
	if (bOnline)
	{
		bRequireTrustedSessions = true;
		bRequireDungeonTicket |= !OnlineDungeonId.IsNone();
	}
	const auto* Driver = Cast<UProject_JIpNetDriver>(GetWorld()->GetNetDriver());
	const int32 RequestedCapacity = FMath::Max(1, MaxTrustedSessions);
	ChannelCapacity = Driver ? FMath::Min(RequestedCapacity, Driver->GetIncomingCapacity()) : RequestedCapacity;
	const bool bCapacityConfigured = SessionGate.SetCapacity(ChannelCapacity);
	ensureMsgf(bCapacityConfigured, TEXT("Trusted session capacity must be configured before any login"));
	Super::InitGameState();
	if (GameSession && (GetNetMode() == NM_DedicatedServer || bOnline))
	{
		GameSession->MaxPlayers = ChannelCapacity;
		UE_LOG(LogGameMode, Display, TEXT("Project J channel capacity zone=%s channel=%d requested=%d effective=%d"),
			*GetWorld()->GetMapName(), OnlineChannelId, RequestedCapacity, ChannelCapacity);
	}

	if (AProject_JGameState* ProjectGameState = GetGameState<AProject_JGameState>())
	{
		ProjectGameState->SetWorldInstanceId(CreatePrototypeWorldInstanceId());
	}
}

void AProject_JGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (IsRunningDedicatedServer() && !FApp::ShouldUseThreadingForPerformance())
	{
		UE_LOG(LogGameMode, Warning,
			TEXT("Project J dedicated server started without -useperfthreads: FApp::ShouldUseThreadingForPerformance() is false, forcing serial GameThread animation/task fallback. Pass -useperfthreads in production/staging launch scripts for full multicore throughput."));
	}
	// Ensure every live world has at least one fallback graveyard (first APlayerStart) so dead players are never trapped.
	if (UWorld* World = GetWorld())
	{
		if (auto* Revive = World->GetSubsystem<UProject_JDeathReviveSubsystem>())
		{
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				if (IsValid(*It))
				{
					FProject_JGraveyardPoint DefaultPoint;
					DefaultPoint.GraveyardId = TEXT("Default_PlayerStart");
					DefaultPoint.Location = It->GetActorLocation();
					DefaultPoint.Rotation = It->GetActorRotation();
					Revive->RegisterGraveyard(DefaultPoint);
					break;
				}
			}
		}
	}
	if (!UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld())) return;
	if (auto* Integrity = GetGameInstance()->GetSubsystem<UProject_JServerIntegritySubsystem>())
		Integrity->SetAntiCheatBridge(MakeShared<FProject_JBackendIntegrityBridge>(GetWorld()));
	if (OnlineAdvertisedEndpoint.IsEmpty()) return;
	OnlineIncarnation = FGuid::NewGuid();
	GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>()->SetOnlineChannelId(OnlineChannelId);
	PublishOnlineHeartbeat();
	OnlineHeartbeat = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		if (FPlatformTime::Seconds() >= NextOnlineHeartbeat) PublishOnlineHeartbeat();
		return true;
	}));
}

void AProject_JGameMode::PublishOnlineHeartbeat()
{
	if (!OnlineIncarnation.IsValid()) return;
	NextOnlineHeartbeat = FPlatformTime::Seconds() + 5.;
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("zone"), GetWorld()->GetMapName());
	Body->SetNumberField(TEXT("channel"), OnlineChannelId);
	Body->SetStringField(TEXT("worker"), UProject_JOnlineWorldSubsystem::GuidText(OnlineIncarnation));
	Body->SetStringField(TEXT("endpoint"), OnlineAdvertisedEndpoint);
	Body->SetNumberField(TEXT("population"), GetNumPlayers());
	Body->SetNumberField(TEXT("capacity"), ChannelCapacity);
	Body->SetBoolField(TEXT("draining"), IsServerDraining());
	Body->SetBoolField(TEXT("is_dungeon"), bRequireDungeonTicket);
	UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("World"), TEXT("Channel_Heartbeat"), Body, {},
		[WeakThis = TWeakObjectPtr<ThisClass>(this)](const TSharedPtr<FJsonObject>& Ack)
		{
			auto* Mode = WeakThis.Get();
			FString Status;
			if (Mode && !Mode->bSessionsClosing && !Mode->OnlineDungeonId.IsNone()
				&& Ack && Ack->TryGetStringField(TEXT("status"), Status) && Status == TEXT("OK"))
				Mode->GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>()->PollOnlineWorker(
					Mode->GetWorld(), Mode->OnlineIncarnation, Mode->OnlineAdvertisedEndpoint, Mode->OnlineDungeonId);
		});
}

bool AProject_JGameMode::RedeemOnlineLogin(APlayerController* Player, FGuid Ticket)
{
	auto* State = Player ? Player->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!OnlineIncarnation.IsValid() || !Ticket.IsValid() || !IsSessionPlayer(Player, State) || Player->GetPawn()
		|| State->GetCharacterId().IsValid() || OnlineLoginPending.Contains(Player) || OnlineLoginPending.Num() >= ChannelCapacity) return false;
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), UProject_JOnlineWorldSubsystem::GuidText(Ticket));
	Body->SetStringField(TEXT("zone"), GetWorld()->GetMapName());
	Body->SetNumberField(TEXT("channel"), OnlineChannelId);
	Body->SetStringField(TEXT("worker"), UProject_JOnlineWorldSubsystem::GuidText(OnlineIncarnation));
	if (const auto* Travel = PendingDungeonTickets.Find(Player))
	{
		auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
		if (!Dungeon || !Dungeon->CanAdmitJoinTicket(*Travel, Dungeon->GetCurrentUtcEpochSeconds())) return false;
		Body->SetStringField(TEXT("return_character"), UProject_JOnlineWorldSubsystem::GuidText(Travel->CharacterId));
		if (Travel->bReturnToWorld)
		{
			Body->SetStringField(TEXT("return_nonce"), UProject_JOnlineWorldSubsystem::GuidText(Travel->Nonce));
			Body->SetNumberField(TEXT("return_expires"), Travel->ExpiresAtSeconds);
			Body->SetStringField(TEXT("return_instance"), UProject_JOnlineWorldSubsystem::GuidText(Travel->InstanceId));
		}
		else Body->SetStringField(TEXT("dungeon_instance"), UProject_JOnlineWorldSubsystem::GuidText(Travel->InstanceId));
	}
	const TWeakObjectPtr<APlayerController> WeakPlayer(Player);
	const TWeakObjectPtr<AProject_JGameMode> WeakThis(this);
	OnlineLoginPending.Add(WeakPlayer);
	const bool bStarted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("World"), TEXT("Redeem_Login"), Body, Ticket,
		[WeakThis, WeakPlayer](const TSharedPtr<FJsonObject>& Response)
		{
			auto* Mode = WeakThis.Get();
			if (!Mode) return;
			const bool bWasPending = Mode->OnlineLoginPending.Remove(WeakPlayer) > 0;
			auto* PC = WeakPlayer.Get();
			if (!bWasPending || !IsValid(PC) || PC->IsActorBeingDestroyed()) return;
			if (!Response.IsValid() || !Mode->InstallOnlineLogin(PC, *Response))
			{
				if (!Response.IsValid()) UE_LOG(LogGameMode, Warning, TEXT("Online login service did not return a response."));
				PC->ClientReturnToMainMenuWithTextReason(NSLOCTEXT("ProjectJOnline", "LoginFailed", "The online character could not be opened."));
			}
		});
	if (!bStarted) OnlineLoginPending.Remove(WeakPlayer);
	return bStarted;
}

bool AProject_JGameMode::InstallOnlineLogin(APlayerController* PC, const FJsonObject& Response)
{
	FString Status, AccountId, CharacterId, EpochText, ClassId, Experience;
	FProject_JAccountId Account;
	FProject_JCharacterId Character;
	FProject_JProgressionSnapshot Progression;
	int64 Epoch = 0;
	if (!Response.TryGetStringField(TEXT("status"), Status) || Status != TEXT("OK"))
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login was rejected by the session store."));
		return false;
	}
	if (!Response.TryGetStringField(TEXT("account"), AccountId) || !FGuid::Parse(AccountId, Account.Value)
		|| !Response.TryGetStringField(TEXT("character"), CharacterId) || !FGuid::Parse(CharacterId, Character.Value)
		|| !Response.TryGetStringField(TEXT("epoch"), EpochText) || !LexTryParseString(Epoch, *EpochText) || Epoch <= 0
		|| !Response.TryGetStringField(TEXT("class"), ClassId) || !Response.TryGetNumberField(TEXT("level"), Progression.Level)
		|| !Response.TryGetStringField(TEXT("xp"), Experience) || !LexTryParseString(Progression.Experience, *Experience))
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login returned an invalid profile shape."));
		return false;
	}
	auto* State = PC->GetPlayerState<AProject_JPlayerState>();
	if (!IsSessionPlayer(PC, State)) return false;
	if (const auto* Previous = BoundSessions.Find(Account.Value); Previous && Previous->Lease.CharacterId == Character.Value
		&& OnlineEpochs.FindRef(Previous->State.Get()) >= Epoch) return false;
	auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!Data || (!Data->ArePrimaryAssetDefinitionsReady() && !Data->LoadPrimaryAssetDefinitions()))
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login could not prepare gameplay definitions."));
		return false;
	}
	Progression.ClassId = FName(*ClassId, FNAME_Find);
	auto* Class = Data->GetClassDefinition(Progression.ClassId);
	if (!Class)
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login could not restore the character progression."));
		return false;
	}
	ProjectJ::MMO::FServerSessionAttempt Attempt;
	ProjectJ::MMO::FServerSessionLease Lease;
	if (!BeginTrustedSession(PC, Account, Character, Attempt) || !CompleteTrustedSession(Attempt, true, Lease))
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login could not bind the trusted Unreal session."));
		return false;
	}
	const auto Current = [this, State, Lease] { return IsTrustedSessionCurrent(State, Lease); };
	State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, nullptr);
	bool bProgressionChanged = false;
	if (!State->ProgressionComponent->TryInstallCommittedProgression(Progression, Class, {}, Current, bProgressionChanged)
		|| !UProject_JOnlineWorldSubsystem::ApplyInventory(State, Response, Current))
	{
		UE_LOG(LogGameMode, Warning, TEXT("Online login could not install the initial profile projection."));
		RevokeTrustedSession(State, Lease);
		return false;
	}
	// The FastArray is a projection. Existing local writers cannot change it in online mode.
	const auto RejectLocalWrite = [] { return false; };
	const TWeakObjectPtr<AProject_JPlayerState> WeakState(State);
	const TWeakObjectPtr<AProject_JGameMode> WeakThis(this);
	auto* Inventory = State->GetInventoryComponent();
	const auto Admit = [WeakThis, WeakState]
	{
		const auto* Mode = WeakThis.Get();
		return Mode && Mode->GetOnlineEpoch(WeakState.Get()) > 0;
	};
	const auto SubmitEdit = [WeakThis, WeakState](const FProject_JPersistentEdit& Edit, FProject_JPersistentCompletion Complete)
	{
		auto* Mode = WeakThis.Get();
		return Mode && Mode->SubmitOnlineInventoryEdit(WeakState.Get(), Edit, MoveTemp(Complete));
	};
	Inventory->PersistentAdmission = Admit;
	Inventory->SubmitPersistentEdit = SubmitEdit;
	auto* Equipment = State->GetEquipmentManagerComponent();
	Equipment->bPersistentProfileBound = true;
	Equipment->PersistentAdmission = Admit;
	Equipment->SubmitPersistentEdit = SubmitEdit;
	State->ProgressionComponent->PersistentAdmission = RejectLocalWrite;
	auto* Jobs = State->GetCraftingJobsComponent();
	Jobs->PersistentAdmission = Admit;
	Jobs->SubmitPersistentEdit = SubmitEdit;
	Jobs->CreatePersistentRequest = [] { return FGuid::NewGuid(); };
	State->QuestComponent->PersistentAdmission = Admit;
	State->QuestComponent->SubmitPersistentEdit = SubmitEdit;
	State->QuestComponent->CreatePersistentRequest = [] { return FGuid::NewGuid(); };
	State->QuestComponent->SubmitPersistentProgress = [WeakThis, WeakState](const TArray<FProject_JQuestProgressInput>& Inputs)
	{
		auto* Mode = WeakThis.Get();
		return Mode && Mode->SubmitOnlineQuestProgress(WeakState.Get(), Inputs);
	};
	OnlineEpochs.Add(State, Epoch);
	if (bProgressionChanged) State->ProgressionComponent->PublishCommittedExperience(Current);
	if (!Current()) return false;
	HandleStartingNewPlayer(PC);
	return true;
}

bool AProject_JGameMode::SubmitOnlineInventoryEdit(AProject_JPlayerState* Player, const FProject_JPersistentEdit& Edit,
	FProject_JPersistentCompletion Complete, FProject_JProfileDurableCompletion OnDurable)
{
	using Kind = EProject_JPersistentEdit;
	const int64 Epoch = GetOnlineEpoch(Player);
	if (Epoch <= 0 || !Edit.Request.IsValid()) return false;
	auto Body = MakeShared<FJsonObject>();
	auto* Inventory = Player->GetInventoryComponent();
	FName Route;
	if (Edit.Kind == Kind::AcceptCompoundQuest || Edit.Kind == Kind::SetQuestTracking)
	{
		if (!ProjectJ::OnlineQuests::Prepare(*Player->QuestComponent, Edit, *Body)) return false;
		Route = TEXT("Quest_Edit");
	}
	else if (Edit.Kind == Kind::Claim)
	{
		if (Edit.Quest.IsNone()) return false;
		Body->SetStringField(TEXT("kind"), TEXT("claim"));
		Body->SetStringField(TEXT("quest"), Edit.Quest.ToString());
		Route = TEXT("Quest_Edit");
	}
	else if (Edit.Kind == Kind::Craft || Edit.Kind == Kind::StartCraftJob || Edit.Kind == Kind::CollectCraftJob
		|| Edit.Kind == Kind::CancelCraftJob || Edit.Kind == Kind::Gather)
	{
		auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
		if (!Data || !ProjectJ::OnlineCrafting::PrepareCommand(*Player, *Data, Edit, Player->OnlineInventoryRevision, *Body)) return false;
		Route = TEXT("Craft_Commit");
	}
	else if (Edit.Kind == Kind::Equip || Edit.Kind == Kind::Unequip)
	{
		FProject_JItemInstanceData Item, CurrentSlot;
		if (!Inventory->FindItemInstance(Edit.Instance, Item) || !Item.IsValid() || Item.StackCount != 1) return false;
		const auto* Gear = Cast<UProject_JEquipmentItemDefinition>(Item.ItemDef);
		if (!Gear || Gear->EquipmentSlot != Edit.Slot || Edit.Slot == EProject_JEquipmentSlot::None) return false;
		const bool bEquip = Edit.Kind == Kind::Equip;
		if (bEquip && ((Item.bIsLocked && !Item.bIsEquipped)
			|| (Gear->CombatStyleDefinition && !Gear->CombatStyleDefinition->IsRuntimeReady()))) return false;
		if (bEquip && Player->GetEquipmentManagerComponent()->GetEquippedItemInstance(Edit.Slot, CurrentSlot))
			Body->SetStringField(TEXT("expected_slot_item"), UProject_JOnlineWorldSubsystem::GuidText(CurrentSlot.InstanceId));
		Body->SetStringField(TEXT("item"), UProject_JOnlineWorldSubsystem::GuidText(Edit.Instance));
		Body->SetStringField(TEXT("definition"), Gear->ItemId.ToString());
		Body->SetNumberField(TEXT("version"), Gear->SchemaVersion);
		Body->SetNumberField(TEXT("slot"), int32(Edit.Slot));
		Body->SetBoolField(TEXT("equip"), bEquip);
		Route = TEXT("Equipment_Edit");
	}
	else
	{
		FProject_JItemInstanceData Item;
		if (!Inventory->FindItemInstance(Edit.Instance, Item) || !Item.IsValid() || Item.bIsLocked || Item.bIsEquipped) return false;
		const TCHAR* Operation = nullptr;
		if (Edit.Kind == Kind::Use && Inventory->CanUsePersistentItem(Edit.Instance)) Operation = TEXT("use");
		if (Edit.Kind == Kind::Bag)
		{
			switch (static_cast<EProject_JBagOperation>(Edit.Operation))
			{
			case EProject_JBagOperation::Split: Operation = TEXT("split"); break;
			case EProject_JBagOperation::Merge: Operation = TEXT("merge"); break;
			case EProject_JBagOperation::Swap: Operation = TEXT("swap"); break;
			}
		}
		if (!Operation || Edit.Amount < 0 || Edit.Amount > MAX_int32) return false;
		Body->SetStringField(TEXT("edit"), Operation);
		Body->SetStringField(TEXT("source"), UProject_JOnlineWorldSubsystem::GuidText(Edit.Instance));
		if (Edit.Target.IsValid()) Body->SetStringField(TEXT("target"), UProject_JOnlineWorldSubsystem::GuidText(Edit.Target));
		Body->SetNumberField(TEXT("source_count"), Edit.SourceCount);
		Body->SetNumberField(TEXT("target_count"), Edit.TargetCount);
		Body->SetNumberField(TEXT("source_order"), Edit.SourceOrder);
		Body->SetNumberField(TEXT("target_order"), Edit.TargetOrder);
		Body->SetNumberField(TEXT("amount"), Edit.Amount);
		Body->SetNumberField(TEXT("max_stack"), Item.ItemDef->MaxStackCount);
		Route = TEXT("Bag_Edit");
	}
	return SubmitOnlineSnapshotEdit(Player, Route, Body, Edit.Request, MoveTemp(Complete), MoveTemp(OnDurable));
}

bool AProject_JGameMode::SubmitOnlineQuestProgress(AProject_JPlayerState* Player, TConstArrayView<FProject_JQuestProgressInput> Inputs)
{
	const int64 Epoch = GetOnlineEpoch(Player);
	if (Epoch <= 0) return false;
	if (Inputs.IsEmpty()) return true;
	auto Body = MakeShared<FJsonObject>();
	if (!ProjectJ::OnlineQuests::PrepareProgress(*Player->QuestComponent, Inputs, *Body)) return false;
	const FGuid Character = Player->GetCharacterId().Value;
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
	Body->SetStringField(TEXT("earned_at"), LexToString(FDateTime::UtcNow().ToUnixTimestamp()));
	return UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(GetWorld(), TEXT("World"), TEXT("Quest_Progress"),
		Body, FGuid::NewGuid(), TEXT("earned quest progress"),
		[WeakMode = TWeakObjectPtr<ThisClass>(this), WeakPlayer = TWeakObjectPtr<AProject_JPlayerState>(Player),
			Lease = Player->CaptureServerSession(), Character, Epoch](bool bConfirmed, TSharedPtr<FJsonObject> Snapshot)
		{
			if (!bConfirmed || !Snapshot) return;
			const auto Current = [WeakPlayer, Lease] { return WeakPlayer.IsValid() && WeakPlayer->IsServerSessionCurrent(Lease); };
			FString AppliedEpoch;
			if (Current() && Snapshot->TryGetStringField(TEXT("quest_progress_epoch"), AppliedEpoch) && AppliedEpoch == LexToString(Epoch))
			{
				UProject_JOnlineWorldSubsystem::ApplyInventory(WeakPlayer.Get(), *Snapshot, Current);
			}
			else if (const auto* Mode = WeakMode.Get())
			{
				Mode->PublishRecoveredQuestProgress(Character, *Snapshot);
			}
		});
}

void AProject_JGameMode::PublishRecoveredQuestProgress(FGuid Character, const FJsonObject& Snapshot) const
{
	if (!GetWorld() || GetWorld()->bIsTearingDown || bSessionsClosing) return;
	FString AppliedEpoch;
	int64 Epoch = 0;
	if (!Snapshot.TryGetStringField(TEXT("quest_progress_epoch"), AppliedEpoch) || !LexTryParseString(Epoch, *AppliedEpoch)
		|| Epoch <= 0) return;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Controller = It->Get();
		auto* Player = Controller ? Controller->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (GetOnlineEpoch(Player) != Epoch || Player->GetCharacterId().Value != Character) continue;
		const auto Current = [WeakPlayer = TWeakObjectPtr<AProject_JPlayerState>(Player), Lease = Player->CaptureServerSession()]
		{
			return WeakPlayer.IsValid() && WeakPlayer->IsServerSessionCurrent(Lease);
		};
		UProject_JOnlineWorldSubsystem::ApplyInventory(Player, Snapshot, Current);
		return;
	}
}

bool AProject_JGameMode::SubmitOnlineSnapshotEdit(AProject_JPlayerState* Player, FName Route, const TSharedRef<FJsonObject>& Body, FGuid Key,
	FProject_JPersistentCompletion Complete, FProject_JProfileDurableCompletion OnDurable)
{
	const int64 Epoch = GetOnlineEpoch(Player);
	if (Epoch <= 0) return false;
	const FGuid Character = Player->GetCharacterId().Value;
	const auto* Online = GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	if (HasOnlineSnapshotEdit(Character) || (Online && Online->HasOutstandingQuestProgress(Character))) return false;
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
	const auto Lease = Player->CaptureServerSession();
	const TWeakObjectPtr<AProject_JPlayerState> WeakPlayer(Player);
	const TWeakObjectPtr<AProject_JGameMode> WeakThis(this);
	OnlineInventoryPending.Add(Character, WeakPlayer);
	const bool bStarted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("World"), Route, Body, Key,
		[WeakThis, WeakPlayer, Lease, Key, Character, Done = MoveTemp(Complete), Durable = MoveTemp(OnDurable)](const TSharedPtr<FJsonObject>& Response)
		{
			FString Status;
			const bool bAnswered = Response && Response->TryGetStringField(TEXT("status"), Status);
			const bool bCommitted = bAnswered && (Status == TEXT("COMMITTED") || Status == TEXT("REPLAYED"));
			if (Durable)
			{
				FProject_JProfileDurableResult Result;
				Result.Request = Key;
				if (bCommitted) Result.Outcome = EProject_JProfileDurableOutcome::Committed;
				else if (Status == TEXT("STALE") || Status == TEXT("CONFLICT") || Status == TEXT("INVALID")
					|| Status == TEXT("KEY_REUSED") || Status == TEXT("PRE_DISPATCH_REJECTED"))
					Result.Outcome = EProject_JProfileDurableOutcome::ProvenAbsent;
				Durable(Result);
			}
			auto* Mode = WeakThis.Get();
			auto* State = WeakPlayer.Get();
			if (!Mode) return;
			const auto Current = [WeakPlayer, Lease] { const auto* PS = WeakPlayer.Get(); return PS && PS->IsServerSessionCurrent(Lease); };
			if (Mode->OnlineInventoryPending.FindRef(Character) != WeakPlayer || (State && !Current())) return;
			Mode->ReleaseOnlineSnapshotEdit(Character, WeakPlayer);
			if (!State) return;
			const bool bInstalled = (bCommitted || (bAnswered && Status == TEXT("STALE")))
				&& UProject_JOnlineWorldSubsystem::ApplyInventory(State, *Response, Current);
			const bool bOk = bCommitted && bInstalled;
			if (Done && Current()) Done(bOk, bOk && Status == TEXT("COMMITTED"));
		});
	if (!bStarted) ReleaseOnlineSnapshotEdit(Character, WeakPlayer);
	return bStarted;
}

void AProject_JGameMode::ReleaseOnlineSnapshotEdit(FGuid Character, TWeakObjectPtr<AProject_JPlayerState> Player)
{
	if (OnlineInventoryPending.FindRef(Character) == Player) OnlineInventoryPending.Remove(Character);
}

int64 AProject_JGameMode::GetOnlineEpoch(const AProject_JPlayerState* Player) const
{
	return IsValid(Player) && Player->CanAcceptSessionCommand() ? OnlineEpochs.FindRef(Player) : 0;
}

void AProject_JGameMode::NotifyServerDraining()
{
	if (OnlineIncarnation.IsValid())
	{
		PublishOnlineHeartbeat();
	}
}

bool AProject_JGameMode::IsServerDraining() const
{
	if (bSessionsClosing) return true;
	const UGameInstance* GI = GetGameInstance();
	const auto* Drain = GI ? GI->GetSubsystem<UProject_JServerDrainSubsystem>() : nullptr;
	return Drain && Drain->IsDraining();
}

void AProject_JGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty() && IsServerDraining())
	{
		ErrorMessage = TEXT("ServerDraining");
		return;
	}
	const bool bReturnRequest = UGameplayStatics::HasOption(Options, TEXT("TicketReturn"));
	if (!ErrorMessage.IsEmpty() || (!bRequireDungeonTicket && !bReturnRequest))
	{
		return;
	}

	auto* GI = GetGameInstance();
	auto* Dungeon = GI ? GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
	FProject_JDungeonJoinTicket Ticket;
	const double Now = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
	if (!bRequireTrustedSessions || !Dungeon || !Dungeon->ParseJoinTicketOptions(Options, Ticket)
		|| Ticket.bReturnToWorld == bRequireDungeonTicket
		|| !Dungeon->CanAdmitJoinTicket(Ticket, Now))
	{
		ErrorMessage = TEXT("InvalidOrExpiredDungeonTicket");
	}
}

FString AProject_JGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	const bool bReturnRequest = UGameplayStatics::HasOption(Options, TEXT("TicketReturn"));
	if (!Error.IsEmpty() || (!bRequireDungeonTicket && !bReturnRequest)) return Error;
	auto* Dungeon = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
	const double Now = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
	FProject_JDungeonJoinTicket Ticket;
	if (!IsValid(NewPlayerController) || !bRequireTrustedSessions || !Dungeon || !Dungeon->ParseJoinTicketOptions(Options, Ticket)
		|| Ticket.bReturnToWorld == bRequireDungeonTicket
		|| !Dungeon->CanAdmitJoinTicket(Ticket, Now)) return TEXT("InvalidOrExpiredDungeonTicket");

	for (auto It = PendingDungeonTickets.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || It.Value().ExpiresAtSeconds <= Now) It.RemoveCurrent();
	if (PendingDungeonTickets.Num() >= ChannelCapacity) return TEXT("DungeonLoginAtCapacity");
	PendingDungeonTickets.Add(NewPlayerController, MoveTemp(Ticket));
	return {};
}

void AProject_JGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (bRequireTrustedSessions)
	{
		if (auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr) State->RequireServerSession(this);
		Super::PostLogin(NewPlayer); // Normal native login; starting-player admission below holds the Pawn.
		if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
			if (auto* ProjectPlayer = Cast<AProject_JPlayerController>(NewPlayer)) ProjectPlayer->ClientRequestOnlineLogin();
		return;
	}
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* State = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		TWeakObjectPtr<AProject_JGameMode> WeakThis(this); TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
		auto Ready = [WeakThis, WeakPlayer](bool Success)
		{
			auto* Mode = WeakThis.Get(); auto* PC = WeakPlayer.Get();
			if (!IsValid(Mode) || Mode->IsActorBeingDestroyed() || !IsValid(PC) || PC->IsActorBeingDestroyed()) return;
			if (Success) Mode->HandleStartingNewPlayer(PC);
			else
			{
				auto* Status=Mode->GetGameInstance()?Mode->GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>():nullptr;
				const FText Reason=Status?Status->GetStatusText(PC->GetPlayerState<AProject_JPlayerState>()):FText::GetEmpty();
				PC->ClientReturnToMainMenuWithTextReason(Reason.IsEmpty()?NSLOCTEXT("LocalProfile", "LoadFailed", "Local profile could not be loaded. Existing saved data was preserved."):Reason);
			}
		};
#if UE_BUILD_SHIPPING
		Ready(false); // Local names never establish production account authority.
#else
		if (Profiles && State) Profiles->OpenProfile(State, LocalProfile, MoveTemp(Ready));
		else Ready(false);
#endif
		Super::PostLogin(NewPlayer); // Starting-player override holds spawn until restoration completes.
		return;
	}
	Super::PostLogin(NewPlayer);

	AProject_JPlayerState* ProjectPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!ProjectPlayerState)
	{
		return;
	}

	if (!ProjectPlayerState->GetAccountId().IsValid() || !ProjectPlayerState->GetCharacterId().IsValid())
	{
		AssignPrototypeIdentity(*ProjectPlayerState);
	}
}

void AProject_JGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (bRequireTrustedSessions)
	{
		const auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (!IsValid(State) || !State->CanAcceptSessionCommand()) return;
	}
	if (auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr)
	{
		const auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (Profiles->IsBound(State) && !Profiles->IsReady(State)) return;
	}
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		if (!Profiles || !NewPlayer || !Profiles->IsReady(NewPlayer->GetPlayerState<AProject_JPlayerState>())) return;
	}
	if (const auto* Return = PendingDungeonReturns.Find(NewPlayer))
	{
		const FPendingDungeonReturn Pending = *Return;
		const auto* State = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (!IsValid(State) || State->GetCharacterId().Value != Pending.Ticket.CharacterId || NewPlayer->GetPawn()) return;
		RestartPlayerAtTransform(NewPlayer, FTransform(Pending.Anchor.Rotation, Pending.Anchor.Location));
		if (IsValid(NewPlayer) && IsValid(NewPlayer->GetPawn()))
		{
			auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
			if (Dungeon && Dungeon->FinishCharacterReturn(Pending.Ticket)) PendingDungeonReturns.Remove(NewPlayer);
		}
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

UClass* AProject_JGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const auto* State = IsValid(InController) ? InController->GetPlayerState<AProject_JPlayerState>() : nullptr;
	const auto* Progression = State ? State->ProgressionComponent.Get() : nullptr;
	const auto* Job = Progression ? Progression->GetState().ClassDefinition.Get() : nullptr;
	if (Job && !PlayerPawnClassesByJob.IsEmpty())
	{
		if (const auto* PawnClass = PlayerPawnClassesByJob.Find(Job->ClassId))
		{
			const auto* Defaults = PawnClass->GetDefaultObject();
			if (Defaults && Defaults->GetCharacterClassId() == Job->ClassId)
			{
				return PawnClass->Get();
			}
		}
		UE_LOG(LogGameMode, Error, TEXT("Missing or invalid player pawn mapping for job %s"), *Job->ClassId.ToString());
		return nullptr;
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void AProject_JGameMode::Logout(AController* Exiting)
{
	if (auto* Player = Cast<APlayerController>(Exiting))
	{
		OnlineLoginPending.Remove(Player);
		OnlineEpochs.Remove(Player->GetPlayerState<AProject_JPlayerState>());
		if (auto* State = Player->GetPlayerState<AProject_JPlayerState>())
			ReleaseOnlineSnapshotEdit(State->GetCharacterId().Value, State);
		PendingDungeonTickets.Remove(Player);
		PendingDungeonReturns.Remove(Player);
	}
	if (auto* ExitingPS = Exiting ? Exiting->GetPlayerState<AProject_JPlayerState>() : nullptr)
	{
		if (ExitingPS->GetCharacterId().IsValid())
		{
			if (auto* Dungeon = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr)
			{
				if (Dungeon->HasActiveDungeonInstance(ExitingPS->GetCharacterId().Value))
				{
					const double Now = UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds();
					Dungeon->RecordDisconnectInDungeon(ExitingPS->GetCharacterId().Value, Now);
				}
			}
		}
	}
	if (auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr)
	{
		auto* State = Exiting ? Exiting->GetPlayerState<AProject_JPlayerState>() : nullptr;
		Profiles->RequestCheckpoint(State);
		Profiles->Detach(State);
	}
	TArray<ProjectJ::MMO::FServerSessionAttempt> Attempts;
	for (const auto& Pair : PendingSessions) if (Pair.Value.Controller.Get() == Exiting) Attempts.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Attempts) CancelTrustedSession(Attempt);
	TArray<FBoundSession> Bindings;
	for (const auto& Pair : BoundSessions) if (Pair.Value.Controller.Get() == Exiting) Bindings.Add(Pair.Value);
	for (const auto& Binding : Bindings) RevokeTrustedSession(Binding.State.Get(), Binding.Lease);
	Super::Logout(Exiting);
}

bool AProject_JGameMode::IsSessionPlayer(const APlayerController* Player, const AProject_JPlayerState* State) const
{
	return !bSessionsClosing && IsValid(this) && !IsActorBeingDestroyed() && HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
		&& GetWorld()->GetAuthGameMode() == this && IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->HasAuthority()
		&& Player->GetWorld() == GetWorld() && IsValid(State) && !State->IsActorBeingDestroyed() && State->HasAuthority()
		&& State->GetWorld() == GetWorld() && Player->GetPlayerState<AProject_JPlayerState>() == State && State->GetOwner() == Player;
}

bool AProject_JGameMode::CanBindSessionIdentity(const APlayerController* Player, const AProject_JPlayerState* State,
	const FGuid& Account, const FGuid& Character) const
{
	if (!IsSessionPlayer(Player, State)) return false;
	if (State->ServerSessionLease.IsValid())
		return State->ServerSessionAuthority.Get() == this && State->ServerSessionLease.AccountId == Account
			&& State->ServerSessionLease.CharacterId == Character && State->GetAccountId().Value == Account && State->GetCharacterId().Value == Character;
	// First binding never adopts an existing prototype/profile character's data.
	// A different character needs a fresh state; this API performs no migration.
	return !Player->GetPawn() && !State->GetPawn() && !State->GetAccountId().IsValid() && !State->GetCharacterId().IsValid();
}

bool AProject_JGameMode::BeginTrustedSession(APlayerController* Player, const FProject_JAccountId& Account,
	const FProject_JCharacterId& Character, ProjectJ::MMO::FServerSessionAttempt& OutAttempt)
{
	check(IsInGameThread());
	OutAttempt = {};
	auto* State = IsValid(Player) ? Player->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!Account.IsValid() || !Character.IsValid() || !CanBindSessionIdentity(Player, State, Account.Value, Character.Value)) return false;
	// One in-flight attempt per connection. Superseded/disconnected owners do not
	// retain a lifetime-account tombstone or a physical adapter reservation here.
	TArray<ProjectJ::MMO::FServerSessionAttempt> Previous;
	for (const auto& Pair : PendingSessions)
		if (Pair.Value.Controller.Get() == Player || !Pair.Value.Controller.IsValid() || !Pair.Value.State.IsValid()) Previous.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Previous) CancelTrustedSession(Attempt);
	if (SessionGate.Begin(Account.Value, Character.Value, OutAttempt) != ProjectJ::MMO::EAdmission::Accepted) return false;
	PendingSessions.Add(Account.Value, {OutAttempt, Player, State});
	State->RequireServerSession(this);
	return true;
}

bool AProject_JGameMode::CancelTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt)
{
	check(IsInGameThread());
	const auto* Found = PendingSessions.Find(Attempt.AccountId);
	if (!Found || !(Found->Attempt == Attempt)) return false;
	PendingSessions.Remove(Attempt.AccountId);
	return SessionGate.Cancel(Attempt);
}

bool AProject_JGameMode::CompleteTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt, bool bOwnershipVerified,
	ProjectJ::MMO::FServerSessionLease& OutLease)
{
	check(IsInGameThread());
	OutLease = {};
	const auto* Found = PendingSessions.Find(Attempt.AccountId);
	if (!Found || !(Found->Attempt == Attempt)) return false;
	const FPendingSession Pending = *Found;
	const TStrongObjectPtr<APlayerController> Player(Pending.Controller.Get());
	const TStrongObjectPtr<AProject_JPlayerState> State(Pending.State.Get());
	if (!bOwnershipVerified || !CanBindSessionIdentity(Player.Get(), State.Get(), Attempt.AccountId, Attempt.CharacterId)) { CancelTrustedSession(Attempt); return false; }
	const auto* Current = BoundSessions.Find(Attempt.AccountId);
	const bool bAlreadyAdmitted = Current && Current->Controller == Pending.Controller && Current->State == Pending.State
		&& Current->Lease.CharacterId == Attempt.CharacterId && IsTrustedSessionCurrent(State.Get(), Current->Lease);
	const auto* DungeonTicket = PendingDungeonTickets.Find(Player.Get());
	const bool bNeedsDungeonTicket = (bRequireDungeonTicket && !bAlreadyAdmitted) || (DungeonTicket && DungeonTicket->bReturnToWorld);
	auto* Dungeon = bNeedsDungeonTicket && GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
	const double Now = bNeedsDungeonTicket ? UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds() : 0.0;
	if (bNeedsDungeonTicket && (!Dungeon || !DungeonTicket || DungeonTicket->CharacterId != Attempt.CharacterId
		|| !Dungeon->CanAdmitJoinTicket(*DungeonTicket, Now)))
	{
		CancelTrustedSession(Attempt);
		return false;
	}
	TOptional<FPendingDungeonReturn> Returning;
	if (DungeonTicket && DungeonTicket->bReturnToWorld)
	{
		FProject_JReturnAnchor Anchor;
		if (!Dungeon->GetReturnPlacement(*DungeonTicket, Now, Anchor))
		{
			CancelTrustedSession(Attempt);
			return false;
		}
		Returning = FPendingDungeonReturn{*DungeonTicket, Anchor};
	}
	ProjectJ::MMO::FServerSessionLease Lease, Replaced;
	if (SessionGate.Accept(Attempt, Lease, Replaced) != ProjectJ::MMO::EAdmission::Accepted)
	{ CancelTrustedSession(Attempt); return false; }
	PendingSessions.Remove(Attempt.AccountId);
	const bool bDungeonAdmitted = !bNeedsDungeonTicket || Dungeon->VerifyAndConsumeJoinTicket(*DungeonTicket, Now);
	PendingDungeonTickets.Remove(Player.Get());
	TArray<FBoundSession> Retired;
	for (const auto& Pair : BoundSessions)
		if (Pair.Value.Lease == Replaced || Pair.Value.Controller == Pending.Controller || Pair.Value.State == Pending.State) Retired.Add(Pair.Value);
	for (const auto& Binding : Retired)
	{
		BoundSessions.Remove(Binding.Lease.AccountId);
		SessionGate.Release(Binding.Lease); // A same-account replacement already revoked this exact epoch.
	}
	if (!bDungeonAdmitted)
	{
		SessionGate.Release(Lease);
		for (const auto& Binding : Retired) RetireSession(Binding);
		return false;
	}
	// Publish the new native owner before callbacks. A nested acceptance can now
	// replace it; outer cleanup/install must never overwrite that newer lease.
	BoundSessions.Add(Lease.AccountId, {Lease, Pending.Controller, Pending.State});
	if (Returning.IsSet()) PendingDungeonReturns.Add(Pending.Controller, MoveTemp(Returning.GetValue()));
	for (const auto& Binding : Retired) RetireSession(Binding);
	if (!IsTrustedSessionCurrent(State.Get(), Lease)) { RevokeTrustedSession(State.Get(), Lease); return false; }
	State->InstallServerSession(this, Lease);
	FProject_JAccountId Account; Account.Value = Lease.AccountId;
	FProject_JCharacterId Character; Character.Value = Lease.CharacterId;
	State->SetIdentity(Account, Character);
	if (!State->IsServerSessionCurrent(Lease)) { RevokeTrustedSession(State.Get(), Lease); return false; }
	OutLease = Lease;
	return true;
}

bool AProject_JGameMode::IsTrustedSessionCurrent(const AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& Lease) const
{
	check(IsInGameThread());
	const auto* Found = BoundSessions.Find(Lease.AccountId);
	return Found && Found->Lease == Lease && Found->State.Get() == Player
		&& IsSessionPlayer(Found->Controller.Get(), Player) && SessionGate.IsCurrent(Lease);
}

bool AProject_JGameMode::RevokeTrustedSession(AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& ExpectedLease)
{
	check(IsInGameThread());
	if (!ExpectedLease.IsValid()) return false;
	const auto* Found = BoundSessions.Find(ExpectedLease.AccountId);
	if (!Found || !(Found->Lease == ExpectedLease) || Found->State.Get() != Player) return false;
	const FBoundSession Retired = *Found;
	BoundSessions.Remove(ExpectedLease.AccountId);
	SessionGate.Release(ExpectedLease);
	RetireSession(Retired);
	return true;
}

void AProject_JGameMode::PlayerSessionEnded(AProject_JPlayerState* Player)
{
	// Terminal owner cleanup, separate from public exact-lease logout. An old
	// empty capture cannot cancel a newer pending authentication attempt.
	TArray<ProjectJ::MMO::FServerSessionAttempt> Attempts;
	for (const auto& Pair : PendingSessions) if (Pair.Value.State.Get() == Player) Attempts.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Attempts) CancelTrustedSession(Attempt);
	TArray<FBoundSession> Bindings;
	for (const auto& Pair : BoundSessions) if (Pair.Value.State.Get() == Player) Bindings.Add(Pair.Value);
	for (const auto& Binding : Bindings) RevokeTrustedSession(Player, Binding.Lease);
}

void AProject_JGameMode::RetireSession(const FBoundSession& Binding)
{
	const TStrongObjectPtr<AProject_JPlayerState> State(Binding.State.Get());
	const TStrongObjectPtr<APlayerController> Player(Binding.Controller.Get());
	auto StillOwns = [&] { return IsValid(State.Get()) && State->OwnsServerSession(this, Binding.Lease); };
	if (IsValid(State.Get()) && !StillOwns()) return; // A newer lease may already own this PlayerState.
	OnlineEpochs.Remove(TWeakObjectPtr<const AProject_JPlayerState>(Binding.State));
	ReleaseOnlineSnapshotEdit(Binding.Lease.CharacterId, Binding.State);
	if (!StillOwns()) return;
	State->bServerSessionRevoked = true;
	if (auto* GI = GetGameInstance())
	{
		if (auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>()) Profiles->Detach(State.Get());
		if (auto* Social = GI->GetSubsystem<UProject_JSocialSubsystem>()) Social->UnbindPlayerState(State.Get());
	}
	if (!StillOwns()) return;
	const TStrongObjectPtr<APawn> ControlledPawn(IsValid(Player.Get()) ? Player->GetPawn() : nullptr);
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(State->GetProjectJAbilitySystemComponent());
	const TStrongObjectPtr<APawn> Pawn(IsValid(ASC.Get()) ? Cast<APawn>(ASC->GetAvatarActor()) : nullptr);
	const uint64 Revision = IsValid(ASC.Get()) ? ASC->GetActorInfoRevision() : 0;
	auto SameAvatar = [&]
	{
		return StillOwns() && IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging()
			&& ASC->GetOwnerActor() == State.Get() && ASC->GetAvatarActor() == Pawn.Get() && ASC->GetActorInfoRevision() == Revision;
	};
	TArray<FGameplayAbilitySpecHandle> Executions;
	if (IsValid(Pawn.Get()) && SameAvatar())
		for (const auto& Spec : ASC->GetActivatableAbilities()) if (Spec.IsActive()) Executions.Add(Spec.Handle);
	// Native possession is its own input authority; ordinary RPC budgets alone
	// cannot revoke Mover's controller input. UnPossessed retires owned PawnScope,
	// sprint and input bindings through the existing character lifecycle.
	if (StillOwns() && IsValid(Player.Get()) && IsValid(ControlledPawn.Get()) && Player->GetPlayerState<AProject_JPlayerState>() == State.Get()
		&& Player->GetPawn() == ControlledPawn.Get() && ControlledPawn->GetController() == Player.Get() && ControlledPawn->GetPlayerState() == State.Get()) Player->UnPossess();
	for (const auto& Handle : Executions)
	{
		if (!SameAvatar()) return;
		ASC->CancelAbilityHandle(Handle);
	}
	if (IsValid(Pawn.Get()) && SameAvatar()) ASC->ClearActorInfo();
}

void AProject_JGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (OnlineHeartbeat.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(OnlineHeartbeat);
	OnlineLoginPending.Reset();
	OnlineEpochs.Reset();
	OnlineInventoryPending.Reset();
	bSessionsClosing = true;
	SessionGate.Close();
	TArray<FBoundSession> Retired; BoundSessions.GenerateValueArray(Retired);
	BoundSessions.Reset(); PendingSessions.Reset(); PendingDungeonTickets.Reset(); PendingDungeonReturns.Reset();
	for (const auto& Binding : Retired) RetireSession(Binding);
	Super::EndPlay(Reason);
}

FProject_JWorldInstanceId AProject_JGameMode::CreatePrototypeWorldInstanceId() const
{
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString(TEXT("UnknownMap"));

	FProject_JWorldInstanceId WorldInstanceId;
	WorldInstanceId.WorldId = FName(TEXT("PrototypeWorld"));
	WorldInstanceId.ZoneId = FName(*FPaths::GetBaseFilename(MapName));
	WorldInstanceId.InstanceId = FName(*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	WorldInstanceId.ChannelId = FName(TEXT("Default"));
	return WorldInstanceId;
}

void AProject_JGameMode::AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const
{
	ProjectPlayerState.SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
}
