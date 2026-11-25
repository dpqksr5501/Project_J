#include "Testing/Project_JOnlineLoginFixture.h"

#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Backend/Project_JServerDrainSubsystem.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Project_JInventoryComponent.h"
#include "Containers/Ticker.h"
#include "Crafting/Project_JCraftingStation.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Gathering/Project_JGatheringNode.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
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
static constexpr int32 DrainFixtureChannel = 4;

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
	GetWorldTimerManager().SetTimer(Timeout, [] { FPlatformMisc::RequestExitWithStatus(false, 7); }, 65.f, false);
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_READY"));
#endif
}

void AProject_JOnlineLoginFixture::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const bool bDrain = IsOnlineDrainFixture();
	if (!bDungeonSource && !bDungeonDestination && !bDrain) return;
	auto* PC = Cast<AProject_JOnlineLoginFixtureController>(NewPlayer);
	auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!PC || GetOnlineEpoch(State) <= 0 || !PC->GetPawn()) return;
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
				if (It->bTriggerDungeonClearOnDefeat && It->Phase == EProject_JEncounterPhase::Active)
					for (TActorIterator<AProject_JNPCCharacter> Boss(GetWorld()); Boss; ++Boss)
						if (Boss->GetClass() == It->NPCClass.Get() && Boss->GetAbilitySystemComponent())
						{
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
	}), 58.f);
	auto* Online = GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	const FString Access = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_TEST_LOGIN_ACCESS"));
	const FString FixtureZone = FParse::Param(FCommandLine::Get(), TEXT("ProjectJOnlineAuthoredSandbox"))
		? TEXT("Test_MMO_Sandbox") : TEXT("Entry");
	Online->OnConnectDispatched.AddUniqueDynamic(this, &ThisClass::ObserveConnection);
	const auto WeakThis = TWeakObjectPtr<ThisClass>(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Online, LoginCharacter, Access, FixtureZone](float)
	{
		const int32 Channel = IsOnlineDrainFixture() ? DrainFixtureChannel : 1;
		if (auto* Controller = WeakThis.Get(); Controller && !Online->ConnectCharacter(Controller, Access, LoginCharacter, FixtureZone, Channel))
			FPlatformMisc::RequestExitWithStatus(false, 4);
		return false;
	}), 1.f);
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
	if (Action != TEXT("MailClaim")) return;
	UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_MAIL_RESULT %s"), *Reason.ToString());
	if (bSuccess) GetWorldTimerManager().SetTimer(Probe, this, &ThisClass::VerifyInventory, .2f, true);
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
		GetWorldTimerManager().ClearTimer(Probe);
		UE_LOG(LogProjectJOnlineLoginFixture, Display, TEXT("ONLINE_FIXTURE_INVENTORY_REPLICATED"));
		FPlatformMisc::RequestExit(false);
	}
}
