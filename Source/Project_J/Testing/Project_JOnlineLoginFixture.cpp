#include "Testing/Project_JOnlineLoginFixture.h"

#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Backend/Project_JServerDrainSubsystem.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Containers/Ticker.h"
#include "Crafting/Project_JCraftingStation.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Gathering/Project_JGatheringNode.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAttributeSet.h"
#include "Project_JNPCCharacter.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldPortal.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJOnlineLoginFixture, Log, All);

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
static const FName OnlineDungeonFixtureId(TEXT("LocalOnlineDungeon"));
static const FName OnlineQuestFixtureId(TEXT("Native.OnlineReward"));
static const FName PriorQuestFixtureId(TEXT("native.quest.ownerhistory"));
static const FName OnlineQuestFixtureEvent(TEXT("Native.OnlineReward.Event"));
static constexpr int32 DrainFixtureChannel = 4;
static constexpr int32 ChatFixtureChannel = 5;

static bool IsOnlineChatFixture()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))
		&& FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineChatFixture"));
}

static bool IsOnlineDrainFixture()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))
		&& FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineDrainFixture"));
}
#endif

AProject_JOnlineLoginFixture::AProject_JOnlineLoginFixture()
{
	PlayerControllerClass = AProject_JOnlineLoginFixtureController::StaticClass();
	DefaultPawnClass = ACharacter::StaticClass();
}

void AProject_JOnlineLoginFixture::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))) return;
	bAuthoredSandbox = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineAuthoredSandbox"));
	if (GetNetMode() == NM_DedicatedServer)
	{
		bDungeonSource = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineDungeonSource"));
		bDungeonDestination = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineDungeonDestination"));
		if ((bDungeonSource || bDungeonDestination) && !bAuthoredSandbox)
		{
			FixtureDungeon = NewObject<UProject_JDungeonDefinition>(this);
			FixtureDungeon->DungeonId = OnlineDungeonFixtureId;
			GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>()->RegisterDungeonDefinition(FixtureDungeon);
		}
		if (bAuthoredSandbox || (!bDungeonSource && !bDungeonDestination))
		{
			DefaultPawnClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
			if (!DefaultPawnClass) { FPlatformMisc::RequestExitWithStatus(false, 6); return; }
		}
	}
	if (!bAuthoredSandbox) GetWorld()->SpawnActor<APlayerStart>(FVector(0., 0., 5000.), FRotator::ZeroRotator);
	FTimerHandle Timeout;
	// The chat runner supervises startup and exercise separately. Keep its
	// process watchdog beyond both wall-time windows on a loaded local host.
	const float TimeoutSeconds = IsOnlineChatFixture() ? 240.f : 65.f;
	GetWorldTimerManager().SetTimer(Timeout, [] { FPlatformMisc::RequestExitWithStatus(false, 7); }, TimeoutSeconds, false);
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_READY"));
#endif
}

void AProject_JOnlineLoginFixture::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const bool bDrain = IsOnlineDrainFixture();
	const bool bVerifyQuest = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyOnlineQuest"));
	if (!bDungeonSource && !bDungeonDestination && !bDrain && !bVerifyQuest) return;
	auto* PC = Cast<AProject_JOnlineLoginFixtureController>(NewPlayer);
	auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!PC || GetOnlineEpoch(State) <= 0 || !PC->GetPawn()) return;
	if (bVerifyQuest)
	{
		PrepareOnlineQuest(*State);
		return;
	}
	if (bDrain)
	{
		// Start outside the login stack so the normal join and possession messages are sent.
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]
		{
			auto* Drain = GetGameInstance()->GetSubsystem<UProject_JServerDrainSubsystem>();
			if (!Drain || !Drain->BeginGracefulDrain(TEXT("LocalVerification"), 5.f, FString(), true))
				FPlatformMisc::RequestExitWithStatus(false, 15);
		}));
		return;
	}
	auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	if (bDungeonSource && bDungeonOutbound)
	{
		if (FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(), FixtureAnchor) > 1.)
		{ FPlatformMisc::RequestExitWithStatus(false, 11); return; }
		bDungeonReturned = true;
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DUNGEON_RETURNED_AT_ANCHOR"));
		PC->ClientFinishOnlineDungeon();
		return;
	}
	const TWeakObjectPtr<AProject_JOnlineLoginFixtureController> WeakPlayer(PC);
	FTimerHandle Delay;
	if (bDungeonDestination || bAuthoredSandbox)
	{
		DungeonPlayer = PC;
		if (bDungeonDestination)
		{
			if (!bAuthoredSandbox && (!FixtureDungeon || !Dungeon->ResolveFinalBossDefeat(*FixtureDungeon)))
			{ FPlatformMisc::RequestExitWithStatus(false, 12); return; }
			UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DUNGEON_DESTINATION_JOINED"));
		}
		else
		{
			if (auto* Quests = State->FindComponentByClass<UProject_JQuestComponent>(); !Quests || !Quests->Accept(TEXT("Prototype.GatherAndCraft")))
			{ FPlatformMisc::RequestExitWithStatus(false, 13); return; }
		}
		GetWorldTimerManager().SetTimer(DungeonProbe, this, &ThisClass::AdvanceDungeonProbe, .2f, true);
		return;
	}
	GetWorldTimerManager().SetTimer(Delay, FTimerDelegate::CreateWeakLambda(this, [this, WeakPlayer]
	{
		auto* Player = WeakPlayer.Get();
		if (!Player || !Player->GetPawn()) return;
		FixtureAnchor = Player->GetPawn()->GetActorLocation();
		bDungeonOutbound = UProject_JMMOClientBridgeComponent::DispatchDungeonEntryForPlayer(Player,
			EProject_JDungeonEntrySource::MenuUI, OnlineDungeonFixtureId, true);
		if (!bDungeonOutbound) FPlatformMisc::RequestExitWithStatus(false, 10);
	}), .75f, false);
#endif
}

void AProject_JOnlineLoginFixture::PrepareOnlineQuest(AProject_JPlayerState& State)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* Quests = State.FindComponentByClass<UProject_JQuestComponent>();
	if (!Quests || Quests->FindDefinition(OnlineQuestFixtureId)) return;
	auto* Definition = NewObject<UProject_JQuestDefinition>(Quests);
	Definition->QuestId = OnlineQuestFixtureId;
	Definition->ObjectiveEvent = OnlineQuestFixtureEvent;
	Definition->RewardExperience = 25;
	auto& Item = Definition->RewardItems.AddDefaulted_GetRef();
	Item.Definition = TEXT("ManaPotion");
	Item.Count = 2;
	Quests->Definitions.Add(Definition);
	GetWorldTimerManager().SetTimer(OnlineQuestProbe, FTimerDelegate::CreateWeakLambda(this,
		[this, Player = TWeakObjectPtr<AProject_JPlayerState>(&State), Lease = State.CaptureServerSession()]
		{
			auto* Owner = Player.Get();
			if (!Owner || !Owner->IsServerSessionCurrent(Lease)) { GetWorldTimerManager().ClearTimer(OnlineQuestProbe); return; }
			auto* Quest = Owner->FindComponentByClass<UProject_JQuestComponent>();
			const auto* Inventory = Owner->GetInventoryComponent();
			if (!Quest || !Inventory || Inventory->GetItemInstances().Num() != 2) return;
			const auto* Progress = Quest->States.FindByPredicate([](const auto& Entry) { return Entry.QuestId == OnlineQuestFixtureId; });
			if (!Progress) { Quest->Accept(OnlineQuestFixtureId); return; }
			if (Quest->TryRecordObjective(OnlineQuestFixtureEvent)) GetWorldTimerManager().ClearTimer(OnlineQuestProbe);
		}), .25f, true);
#endif
}

void AProject_JOnlineLoginFixture::AdvanceDungeonProbe()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* PC = DungeonPlayer.Get();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!Pawn || GetOnlineEpoch(State) <= 0) return;
	auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	if (bDungeonDestination)
	{
		if (bAuthoredSandbox && !bBossDefeatIssued)
		{
			for (TActorIterator<AProject_JWorldEncounter> It(GetWorld()); It; ++It)
			{
				auto* Boss = It->GetActiveNPC();
				if (!It->bTriggerDungeonClearOnDefeat || !Boss || !Boss->GetAbilitySystemComponent()) continue;
				Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0.f);
				bBossDefeatIssued = true;
				UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_SANDBOX_BOSS_DEFEATED"));
				break;
			}
		}
		if (!Dungeon->IsDungeonClearedFor(State->GetCharacterId().Value)) return;
		GetWorldTimerManager().ClearTimer(DungeonProbe);
		PC->FindComponentByClass<UProject_JMMOClientBridgeComponent>()->ServerRequestDungeonReturn();
		return;
	}

	auto* Quests = State->FindComponentByClass<UProject_JQuestComponent>();
	FProject_JQuestDefinitionIndex Definitions;
	Quests->BuildDefinitionIndex(Definitions);
	const auto* Definition = Definitions.FindRef(TEXT("Prototype.GatherAndCraft"));
	const auto Snapshot = Quests->CaptureSnapshot();
	const auto* Progress = Snapshot.States.FindByPredicate([](const auto& Quest) { return Quest.QuestId == TEXT("Prototype.GatherAndCraft"); });
	if (!Definition || !Progress) return;
	const auto* Goal = Definition->Objectives.FindByPredicate([](const auto& Value) { return Value.Kind == EProject_JQuestObjectiveKind::CraftRecipe; });
	if (!Goal) return;
	if (Progress->GetObjectiveCount(Goal->ObjectiveId) == 0)
	{
		if (bCraftDispatched) return;
		for (TActorIterator<AProject_JGatheringNode> Node(GetWorld()); Node; ++Node)
			if (Node->YieldItemId == TEXT("HealthPotion") && !HarvestedTestNodes.Contains(Node->NodeId))
			{
				Pawn->SetActorLocation(Node->GetActorLocation() + FVector(100., 0., 50.));
				if (Node->TryGather(Pawn))
				{
					HarvestedTestNodes.Add(Node->NodeId);
					UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_SANDBOX_GATHER_ADMITTED %s"), *Node->NodeId.ToString());
				}
				return;
			}
		for (TActorIterator<AProject_JCraftingStation> It(GetWorld()); It; ++It)
			if (It->RecipeId == Goal->TargetId && It->RecipeVersion == Goal->TargetVersion)
			{
				Pawn->SetActorLocation(It->GetActorLocation() + FVector(120., 0., 75.));
				bCraftDispatched = It->TryCraft(Pawn);
				return;
			}
		return;
	}
	// A deterministic GAS hit exercises the placed encounter's threat and reward paths independently of input timing.
	for (TActorIterator<AProject_JWorldEncounter> It(GetWorld()); It; ++It)
	{
		if (It->EncounterId != TEXT("Sandbox_FieldBoss_01")) continue;
		if (bFieldDefeatIssued && It->Phase == EProject_JEncounterPhase::Cooldown) break;
		if (bFieldDefeatIssued) return;
		if (It->Phase != EProject_JEncounterPhase::Active)
		{
			Pawn->SetActorLocation(It->GetActorLocation() + FVector(250., 0., 100.));
			return;
		}
		if (auto* Boss = It->GetActiveNPC())
		{
			auto* Source = State->GetAbilitySystemComponent();
			auto* Target = Boss->GetAbilitySystemComponent();
			if (!Source || !Target || !It->BossLootTable) return;
			auto* Damage = NewObject<UGameplayEffect>(this);
			auto& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
			Modifier.Attribute = UProject_JAttributeSet::GetHealthAttribute();
			Modifier.ModifierOp = EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = FScalableFloat(-Target->GetNumericAttribute(Modifier.Attribute));
			Target->ApplyGameplayEffectToSelf(Damage, 1.f, Source->MakeEffectContext());
			bFieldDefeatIssued = true;
			UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_SANDBOX_FIELD_HIT hp=%.0f"), Target->GetNumericAttribute(Modifier.Attribute));
		}
		return;
	}
	if (!bFieldDefeatIssued) return;
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_SANDBOX_CRAFT_PROGRESS %d"), Progress->GetObjectiveCount(Goal->ObjectiveId));
	for (TActorIterator<AProject_JWorldPortal> It(GetWorld()); It; ++It)
		if (It->DungeonId == TEXT("Dungeon_Starter"))
		{
			Pawn->SetActorLocation(It->GetActorLocation() + FVector(90., 0., 40.));
			FixtureAnchor = Pawn->GetActorLocation();
			bDungeonOutbound = It->CanInteract_Implementation(Pawn);
			if (!bDungeonOutbound) { FPlatformMisc::RequestExitWithStatus(false, 14); return; }
			It->Interact_Implementation(Pawn);
			GetWorldTimerManager().ClearTimer(DungeonProbe);
			return;
		}
#endif
}

void AProject_JOnlineLoginFixture::Logout(AController* Exiting)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const auto* State = Exiting ? Exiting->GetPlayerState<AProject_JPlayerState>() : nullptr;
	// The transport has already closed when Logout runs. Identity was installed
	// only after PostgreSQL admission and survives long enough to inspect here.
	const bool bVerified = GetNetMode() == NM_DedicatedServer && State && State->GetCharacterId().IsValid()
		&& FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"));
#endif
	Super::Logout(Exiting);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (bVerified && IsOnlineDrainFixture())
	{
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DRAIN_AUTHORIZED_LOGOUT"));
		return; // The real drain subsystem owns final shutdown after its outbox check.
	}
	if (bVerified && IsOnlineChatFixture())
	{
		if (ChatFixturePhase != TEXT("Complete")) { FPlatformMisc::RequestExitWithStatus(false, 18); return; }
		if (++ChatFixtureLogouts == 2) FPlatformMisc::RequestExit(false);
		return;
	}
	if (bVerified && (!bDungeonSource || bDungeonReturned)) FPlatformMisc::RequestExit(false);
#endif
}

void AProject_JOnlineLoginFixtureController::ClientFinishOnlineDungeon_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))) return;
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DUNGEON_CLIENT_FINISHED"));
	FPlatformMisc::RequestExit(false);
#endif
}

void AProject_JOnlineLoginFixtureController::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!IsLocalController() || GetNetMode() != NM_Standalone || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))) return;
	FGuid LoginCharacter;
	if (!FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_LOGIN_CHARACTER")), LoginCharacter)) return;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		FPlatformMisc::RequestExitWithStatus(false, 7);
		return false;
	}), IsOnlineChatFixture() ? 240.f : 58.f);
	auto* Online = GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	const FString Access = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_LOGIN_ACCESS"));
	const FString FixtureZone = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineAuthoredSandbox"))
		? TEXT("Test_MMO_Sandbox") : TEXT("Entry");
	Online->OnConnectDispatched.AddUniqueDynamic(this, &ThisClass::ObserveConnection);
	Online->OnLoginQueued.AddUniqueDynamic(this, &ThisClass::ObserveQueuePosition);
	const auto WeakThis = TWeakObjectPtr<ThisClass>(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Online, LoginCharacter, Access, FixtureZone](float)
	{
		const int32 Channel = IsOnlineChatFixture() ? ChatFixtureChannel : IsOnlineDrainFixture() ? DrainFixtureChannel : 1;
		if (auto* Controller = WeakThis.Get(); Controller && !Online->ConnectCharacter(Controller, Access, LoginCharacter, FixtureZone, Channel))
			FPlatformMisc::RequestExitWithStatus(false, 4);
		return false;
	}), 1.f);
#endif
}

void AProject_JOnlineLoginFixtureController::ObserveQueuePosition(int32 Position)
{
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_QUEUE_POSITION %d"), Position);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	FString Output;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJQueueScreenshot="), Output) && !Output.IsEmpty())
		FScreenshotRequest::RequestScreenshot(Output, true, false);
#endif
}

void AProject_JOnlineLoginFixtureController::ObserveConnection(bool bDispatched)
{
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_CONNECTION_REQUEST %d"), bDispatched);
	if (!bDispatched) FPlatformMisc::RequestExitWithStatus(false, 4);
}

void AProject_JOnlineLoginFixtureController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!InPawn || GetNetMode() != NM_Client || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineLoginFixture"))) return;
	if (IsOnlineChatFixture())
	{
		auto* Bridge = FindComponentByClass<UProject_JMMOClientBridgeComponent>();
		FGuid OwnCharacter;
		if (!Bridge || !FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_CHAT_SENDER")), ChatFixtureSender)
			|| !FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_LOGIN_CHARACTER")), OwnCharacter))
		{
			FPlatformMisc::RequestExitWithStatus(false, 18);
			return;
		}
		bChatFixtureReceiver = OwnCharacter != ChatFixtureSender;
		Bridge->OnChatMessageReceived.AddUniqueDynamic(this, &ThisClass::ObserveChatFixture);
		Bridge->OnMMOActionResult.AddUniqueDynamic(this, &ThisClass::ObserveAction);
		ServerReportOnlineChatFixture(TEXT("Ready"));
		return;
	}
	if (IsOnlineDrainFixture())
	{
		const auto* Driver = GetWorld()->GetNetDriver();
		auto* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;
		if (!Connection) { FPlatformMisc::RequestExitWithStatus(false, 16); return; }
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DRAIN_CLIENT_POSSESSED"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Peer = TWeakObjectPtr<UNetConnection>(Connection)](float)
		{
			if (const auto* Live = Peer.Get(); Live && Live->GetConnectionState() != USOCK_Closed) return true;
			UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_DRAIN_CLIENT_DISCONNECTED"));
			FPlatformMisc::RequestExit(false);
			return false;
		}), 0.05f);
		return;
	}
	if (!FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_LOGIN_ITEM")), ExpectedItem)) return;
	if (auto* Bridge = FindComponentByClass<UProject_JMMOClientBridgeComponent>())
	{
		Bridge->OnMailInbox.AddUniqueDynamic(this, &ThisClass::ObserveMailbox);
		Bridge->OnMMOActionResult.AddUniqueDynamic(this, &ThisClass::ObserveAction);
		FTimerHandle Delay;
		GetWorldTimerManager().SetTimer(Delay, this, &ThisClass::OpenMailbox, .5f, false);
	}
	FTimerHandle Timeout;
	GetWorldTimerManager().SetTimer(Timeout, [] { FPlatformMisc::RequestExitWithStatus(false, 7); }, 40.f, false);
#endif
}

void AProject_JOnlineLoginFixtureController::OpenMailbox()
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UProject_JPlayerHUDWidget::StaticClass(), true);
	for (auto* Widget : Widgets)
	{
		auto* HUDWidget = Cast<UProject_JPlayerHUDWidget>(Widget);
		if (HUDWidget && HUDWidget->GetOwningPlayer() == this)
		{
			TestHUD = HUDWidget;
			HUDWidget->ExecuteCommand(TEXT("Mail"));
			return;
		}
	}
	FPlatformMisc::RequestExitWithStatus(false, 9);
}

void AProject_JOnlineLoginFixtureController::ObserveMailbox(const TArray<FProject_JInboxEntry>& Entries, FGuid, bool bSuccess)
{
	if (bSuccess && Entries.Num() == 1 && Entries[0].Quantity == 3)
	{
		GetWorldTimerManager().SetTimerForNextTick([Widget = TestHUD]
		{
			if (auto* MailHUD = Widget.Get()) MailHUD->ExecuteCommand(TEXT("MailClaim.0"));
		});
	}
}

void AProject_JOnlineLoginFixtureController::ObserveAction(FName Action, bool bSuccess, FName Reason)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (IsOnlineChatFixture())
	{
		if (!bSuccess)
		{
			UE_LOG(LogProjectJOnlineLoginFixture, Error, TEXT("ONLINE_CHAT_ACTION_FAILED %s %s"), *Action.ToString(), *Reason.ToString());
			FPlatformMisc::RequestExitWithStatus(false, 18);
		}
		else if (bChatFixtureReceiver && (Action == TEXT("Block") || Action == TEXT("Unblock")))
		{
			UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_CHAT_ACK %s"), *Action.ToString());
			ServerReportOnlineChatFixture(Action);
		}
		return;
	}
#endif
	auto* Bridge = FindComponentByClass<UProject_JMMOClientBridgeComponent>();
	if (Action == TEXT("MailClaim"))
	{
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_MAIL_RESULT %s"), *Reason.ToString());
		if (!bSuccess) return;
		if (Bridge && FGuid::Parse(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_CHAT_TARGET")), ChatTestTarget))
		{
			Bridge->ServerBlockCharacter(ChatTestTarget);
			return;
		}
	}
	else if (Bridge && ChatTestTarget.IsValid() && (Action == TEXT("Block") || Action == TEXT("Unblock")))
	{
		if (!bSuccess) { FPlatformMisc::RequestExitWithStatus(false, 17); return; }
		if (Action == TEXT("Unblock"))
		{
			bCompletedUnblock = true;
			Bridge->ServerBlockCharacter(ChatTestTarget);
			return;
		}
		if (!bCompletedUnblock) { Bridge->ServerUnblockCharacter(ChatTestTarget); return; }
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_CHAT_BLOCK_ROUND_TRIP"));
	}
	else return;
	GetWorldTimerManager().SetTimer(Probe, this, &ThisClass::VerifyInventory, .2f, true);
}

void AProject_JOnlineLoginFixtureController::ServerReportOnlineChatFixture_Implementation(FName Event)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (IsOnlineChatFixture() && GetNetMode() == NM_DedicatedServer)
	{
		if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JOnlineLoginFixture>()) Mode->AdvanceOnlineChatFixture(*this, Event);
	}
#endif
}

void AProject_JOnlineLoginFixture::AdvanceOnlineChatFixture(AProject_JOnlineLoginFixtureController& Player, FName Event)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* State = Player.GetPlayerState<AProject_JPlayerState>();
	FGuid SenderId, ReceiverId;
	FString SenderText, ReceiverText;
	if (!IsOnlineChatFixture() || GetNetMode() != NM_DedicatedServer || GetOnlineEpoch(State) <= 0 || !Player.GetPawn()
		|| !FParse::Value(FCommandLine::Get(), TEXT("ProjectJChatSender="), SenderText) || !FGuid::Parse(SenderText, SenderId)
		|| !FParse::Value(FCommandLine::Get(), TEXT("ProjectJChatReceiver="), ReceiverText) || !FGuid::Parse(ReceiverText, ReceiverId)
		|| SenderId == ReceiverId) return;
	const FGuid Character = State->GetCharacterId().Value;
	if (Event == TEXT("Ready") && ChatFixturePhase.IsNone())
	{
		if (Character == SenderId) ChatSender = &Player;
		if (Character == ReceiverId) ChatReceiver = &Player;
		if (ChatSender.IsValid() && ChatReceiver.IsValid())
		{
			ChatFixturePhase = TEXT("Before");
			UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_CHAT_SAME_WORLD_STARTED"));
			ChatSender->ClientAdvanceOnlineChatFixture(TEXT("Before"));
		}
		return;
	}
	if (!ChatSender.IsValid() || !ChatReceiver.IsValid()) return;
	if (ChatReceiver.Get() == &Player && (Event == TEXT("Block") && ChatFixturePhase == TEXT("Before")
		|| Event == TEXT("Unblock") && ChatFixturePhase == TEXT("Blocked")))
	{
		ChatFixturePhase = Event == TEXT("Block") ? TEXT("WhileBlocked") : TEXT("After");
		FTimerHandle Delay;
		// Three messages from the same character exercise the real World rate limit.
		GetWorldTimerManager().SetTimer(Delay, FTimerDelegate::CreateWeakLambda(this, [this, Phase = ChatFixturePhase]
		{
			if (ChatFixturePhase == Phase && ChatSender.IsValid() && ChatReceiver.IsValid())
				ChatSender->ClientAdvanceOnlineChatFixture(Phase);
		}), 10.25f, false);
	}
	else if (ChatSender.Get() == &Player && Event == TEXT("MutedEcho") && ChatFixturePhase == TEXT("WhileBlocked"))
	{
		ChatFixturePhase = TEXT("Blocked");
		// The blocked message was already processed by the server. A reliable
		// owner RPC on the same actor follows any delivery to that receiver.
		ChatReceiver->ClientAdvanceOnlineChatFixture(TEXT("CheckMuted"));
	}
	else if (ChatReceiver.Get() == &Player && Event == TEXT("AfterReceived") && ChatFixturePhase == TEXT("After"))
	{
		ChatFixturePhase = TEXT("Complete");
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_CHAT_SAME_WORLD_COMPLETE"));
		ChatSender->ClientAdvanceOnlineChatFixture(TEXT("Finish"));
		ChatReceiver->ClientAdvanceOnlineChatFixture(TEXT("Finish"));
	}
#endif
}

void AProject_JOnlineLoginFixtureController::ClientAdvanceOnlineChatFixture_Implementation(FName Step)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!IsOnlineChatFixture() || GetNetMode() != NM_Client || !IsLocalController()) return;
	auto* Bridge = FindComponentByClass<UProject_JMMOClientBridgeComponent>();
	if (!Bridge || !ChatFixtureSender.IsValid()) { FPlatformMisc::RequestExitWithStatus(false, 18); return; }
	if (Step == TEXT("Finish")) FPlatformMisc::RequestExit(false);
	else if (bChatFixtureReceiver && Step == TEXT("CheckMuted"))
	{
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_CHAT_MUTED_CHECKPOINT"));
		Bridge->ServerUnblockCharacter(ChatFixtureSender);
	}
	else if (!bChatFixtureReceiver && (Step == TEXT("Before") || Step == TEXT("WhileBlocked") || Step == TEXT("After")))
	{
		Bridge->ServerSendChatMessage({}, EProject_JChatChannel::World, Step.ToString());
	}
#endif
}

void AProject_JOnlineLoginFixtureController::ObserveChatFixture(FGuid SenderId, EProject_JChatChannel Channel, const FString& Message)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (SenderId != ChatFixtureSender || Channel != EProject_JChatChannel::World) return;
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_CHAT_WORLD_RECEIVED %s"), *Message);
	if (!bChatFixtureReceiver)
	{
		if (Message == TEXT("WhileBlocked")) ServerReportOnlineChatFixture(TEXT("MutedEcho"));
		return;
	}
	if (Message == TEXT("WhileBlocked")) { FPlatformMisc::RequestExitWithStatus(false, 18); return; }
	if (Message == TEXT("Before"))
	{
		if (auto* Bridge = FindComponentByClass<UProject_JMMOClientBridgeComponent>()) Bridge->ServerBlockCharacter(SenderId);
	}
	if (Message == TEXT("After")) ServerReportOnlineChatFixture(TEXT("AfterReceived"));
#endif
}

void AProject_JOnlineLoginFixtureController::VerifyInventory()
{
	const auto* State = GetPlayerState<AProject_JPlayerState>();
	auto* Inventory = State ? State->GetInventoryComponent() : nullptr;
	FProject_JItemInstanceData Item;
	if (!Inventory || !Inventory->FindItemInstance(ExpectedItem, Item)) return;
	if (!bSplitRequested && Item.StackCount == 3)
	{
		bSplitRequested = true;
		FProject_JBagRequest Request;
		Request.Source = ExpectedItem;
		Request.Count = 1;
		Request.ExpectedSourceCount = Item.StackCount;
		Request.ExpectedSourceOrder = Item.BagOrder;
		Inventory->RequestBagChange(Inventory->NewRequestId(), Request);
		return;
	}
	if (bSplitRequested && Item.StackCount == 2 && Inventory->GetItemInstances().ContainsByPredicate([&](const auto& Other)
		{ return Other.InstanceId != Item.InstanceId && Other.ItemDef == Item.ItemDef && Other.StackCount == 1; }))
	{
		if (!VerifyQuestReward()) return;
		GetWorldTimerManager().ClearTimer(Probe);
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_INVENTORY_REPLICATED"));
		FPlatformMisc::RequestExit(false);
	}
}

bool AProject_JOnlineLoginFixtureController::VerifyQuestReward()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyOnlineQuest"))) return true;
	const auto* State = GetPlayerState<AProject_JPlayerState>();
	auto* Quest = State ? State->FindComponentByClass<UProject_JQuestComponent>() : nullptr;
	if (!Quest) return false;
	if (!bQuestClaimRequested)
	{
		if (Quest->GetHistorySummary().Count != 1) return false;
		const auto* Entry = Quest->States.FindByPredicate([](const auto& Value) { return Value.QuestId == OnlineQuestFixtureId; });
		if (Entry && Entry->Count == 1)
		{
			bQuestClaimRequested = true;
			Quest->RequestClaim(OnlineQuestFixtureId);
		}
		return false;
	}
	const auto* Progression = State->FindComponentByClass<UProject_JProgressionComponent>();
	const auto* Inventory = State->GetInventoryComponent();
	if (!Progression || Progression->GetExperience() != 40 || !Inventory
		|| !Inventory->GetItemInstances().ContainsByPredicate([](const auto& Item)
			{ return Item.ItemDef && Item.ItemDef->ItemId == TEXT("ManaPotion") && Item.StackCount == 2; })
		|| Quest->GetHistorySummary().Count != 2) return false;
	if (!QuestHistoryRequest.IsValid())
	{
		QuestHistoryRequest = Quest->RequestCompletedPage(0);
		return false;
	}
	const auto Page = Quest->GetHistoryPage();
	if (Page.Status != EProject_JQuestHistoryPageStatus::Fresh || Page.Request != QuestHistoryRequest
		|| Page.Ids.Num() != 2 || Page.Ids[0] != OnlineQuestFixtureId || Page.Ids[1] != PriorQuestFixtureId) return false;
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_QUEST_REWARD_AND_HISTORY_REPLICATED"));
#endif
	return true;
}
