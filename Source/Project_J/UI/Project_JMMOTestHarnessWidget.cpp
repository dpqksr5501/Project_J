#include "UI/Project_JMMOTestHarnessWidget.h"
#include "UI/Project_JPlayerUIComponent.h"

#include "Backend/Project_JNakamaServicePort.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/Project_JCheatManager.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JBaseCharacter.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "World/Project_JWorldEncounter.h"

TSharedRef<SWidget> UProject_JMMOTestHarnessWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		return Super::RebuildWidget();
	}

	const auto* ExistingPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (!WidgetTree->RootWidget || (ExistingPanel && ExistingPanel->GetChildrenCount() == 0))
	{
		UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HarnessRootCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UBorder* PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HarnessBorder"));
		PanelBorder->SetBrushColor(FLinearColor(0.04f, 0.06f, 0.10f, 0.88f));
		PanelBorder->SetPadding(FMargin(12.0f));
		if (UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(PanelBorder))
		{
			PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
			PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
			PanelSlot->SetPosition(FVector2D(-20.0f, 20.0f));
			PanelSlot->SetSize(FVector2D(420.0f, 640.0f));
			PanelSlot->SetAutoSize(true);
		}

		UVerticalBox* MainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HarnessMainBox"));
		PanelBorder->SetContent(MainBox);

		// Title + Close row
		UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
		MainBox->AddChildToVerticalBox(HeaderRow)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

		UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
		TitleText->SetText(FText::FromString(TEXT("Project J - MMO Test Harness")));
		TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.82f, 0.35f, 1.0f)));
		if (UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(TitleText))
		{
			TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TitleSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* CloseBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseBtn"));
		CloseBtn->SetBackgroundColor(FLinearColor(0.65f, 0.15f, 0.15f, 1.0f));
		UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseLabel"));
		CloseLabel->SetText(FText::FromString(TEXT(" X ")));
		CloseBtn->AddChild(CloseLabel);
		CloseBtn->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
		HeaderRow->AddChildToHorizontalBox(CloseBtn);
		ActionButtons.Add(CloseBtn);

		// Telemetry display
		TelemetryText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TelemetryText"));
		TelemetryText->SetText(FText::FromString(TEXT("Loading telemetry...")));
		TelemetryText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.92f, 1.0f, 1.0f)));
		TelemetryText->SetAutoWrapText(true);
		MainBox->AddChildToVerticalBox(TelemetryText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

		auto AddActionButton = [&](const TCHAR* Name, const TCHAR* Label, const FLinearColor& Tint) -> UButton*
		{
			UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			Btn->SetBackgroundColor(Tint);
			UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Txt->SetText(FText::FromString(Label));
			Txt->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Btn->AddChild(Txt);
			MainBox->AddChildToVerticalBox(Btn)->SetPadding(FMargin(0.0f, 2.0f));
			ActionButtons.Add(Btn);
			return Btn;
		};

		const FLinearColor BlueTint(0.14f, 0.34f, 0.58f, 1.0f);
		const FLinearColor GreenTint(0.14f, 0.45f, 0.28f, 1.0f);
		const FLinearColor AmberTint(0.55f, 0.38f, 0.12f, 1.0f);
		const FLinearColor RedTint(0.52f, 0.16f, 0.18f, 1.0f);

		AddActionButton(TEXT("BtnEnterDungeon"), TEXT("[Dungeon] Enter Dungeon_Starter (Menu)"), BlueTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleEnterDungeonClicked);
		AddActionButton(TEXT("BtnReturnWorld"), TEXT("[Dungeon] Return to Open World"), BlueTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleReturnWorldClicked);
		AddActionButton(TEXT("BtnRefreshMail"), TEXT("[Mail] Refresh Inbox"), GreenTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleRefreshMailClicked);
		AddActionButton(TEXT("BtnClaimMail"), TEXT("[Mail] Claim First Mail"), GreenTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleClaimFirstMailClicked);
		AddActionButton(TEXT("BtnHealFull"), TEXT("[Combat] Heal Full HP/MP"), GreenTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleHealFullClicked);
		AddActionButton(TEXT("BtnKillSelf"), TEXT("[Combat] Kill Self (Test State_Dead)"), RedTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleKillSelfClicked);
		AddActionButton(TEXT("BtnReviveGraveyard"), TEXT("[Revive] Request Graveyard Revive"), AmberTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleReviveGraveyardClicked);
		AddActionButton(TEXT("BtnReviveInPlace"), TEXT("[Revive] Request In-Place Revive"), AmberTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleReviveInPlaceClicked);
		AddActionButton(TEXT("BtnToggleFlight"), TEXT("[Mount] Toggle Flight (Mover)"), BlueTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleToggleFlightClicked);
		AddActionButton(TEXT("BtnCycleVFX"), TEXT("[VFX] Cycle OtherPlayer Opacity"), AmberTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleCycleVFXOpacityClicked);
		AddActionButton(TEXT("BtnToggleBackend"), TEXT("[Backend] Toggle Mode (Standalone <-> Nakama)"), AmberTint)
			->OnClicked.AddDynamic(this, &ThisClass::HandleToggleBackendModeClicked);

		// Status log
		StatusLogText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusLogText"));
		StatusLogText->SetText(FText::FromString(TEXT("Status: Ready (use PJ_ToggleTestUI to toggle)")));
		StatusLogText->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.95f, 0.65f, 1.0f)));
		StatusLogText->SetAutoWrapText(true);
		MainBox->AddChildToVerticalBox(StatusLogText)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	}

	return Super::RebuildWidget();
}

void UProject_JMMOTestHarnessWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (auto* PC = GetOwningPlayer())
	{
		if (auto* UI = PC->FindComponentByClass<UProject_JPlayerUIComponent>())
		{
			if (!UI->IsCursorModeEnabled())
			{
				UI->ToggleCursorMode();
				bOwnsCursorMode = UI->IsCursorModeEnabled();
			}
		}
		else
		{
			bPreviousMouseCursor = PC->bShowMouseCursor;
			bOwnsFallbackInput = true;
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
	}
	BindBridgeDelegates();
	RefreshTelemetry();
}

void UProject_JMMOTestHarnessWidget::NativeDestruct()
{
	UnbindBridgeDelegates();
	if (auto* PC = GetOwningPlayer(); PC && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		if (bOwnsCursorMode)
		{
			if (auto* UI = PC->FindComponentByClass<UProject_JPlayerUIComponent>(); UI && UI->IsCursorModeEnabled())
				UI->ToggleCursorMode();
		}
		if (bOwnsFallbackInput)
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = bPreviousMouseCursor;
		}
	}
	bOwnsCursorMode = bOwnsFallbackInput = false;
	Super::NativeDestruct();
}

void UProject_JMMOTestHarnessWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TelemetryAccumulator += InDeltaTime;
	if (TelemetryAccumulator >= 0.5f)
	{
		TelemetryAccumulator = 0.0f;
		RefreshTelemetry();
	}
}

UProject_JMMOClientBridgeComponent* UProject_JMMOTestHarnessWidget::ResolveBridge() const
{
	APlayerController* PC = GetOwningPlayer();
	return IsValid(PC) ? PC->FindComponentByClass<UProject_JMMOClientBridgeComponent>() : nullptr;
}

void UProject_JMMOTestHarnessWidget::BindBridgeDelegates()
{
	UnbindBridgeDelegates();
	if (UProject_JMMOClientBridgeComponent* Bridge = ResolveBridge())
	{
		BoundBridge = Bridge;
		Bridge->OnMMOActionResult.AddDynamic(this, &ThisClass::HandleMMOActionResult);
		Bridge->OnDungeonTicketReceived.AddDynamic(this, &ThisClass::HandleDungeonTicketReceived);
		Bridge->OnMailInbox.AddDynamic(this, &ThisClass::HandleMailInboxReceived);
	}
}

void UProject_JMMOTestHarnessWidget::UnbindBridgeDelegates()
{
	if (UProject_JMMOClientBridgeComponent* Bridge = BoundBridge.Get())
	{
		Bridge->OnMMOActionResult.RemoveDynamic(this, &ThisClass::HandleMMOActionResult);
		Bridge->OnDungeonTicketReceived.RemoveDynamic(this, &ThisClass::HandleDungeonTicketReceived);
		Bridge->OnMailInbox.RemoveDynamic(this, &ThisClass::HandleMailInboxReceived);
	}
	BoundBridge.Reset();
}

void UProject_JMMOTestHarnessWidget::RefreshTelemetry()
{
	if (!TelemetryText) return;
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;

	const FString MapName = World ? World->GetMapName() : TEXT("None");
	const int32 NetMode = World ? static_cast<int32>(World->GetNetMode()) : -1;
	int32 BackendMode = 0;
	if (UGameInstance* GI = World ? World->GetGameInstance() : nullptr)
	{
		if (auto* Port = GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>())
		{
			BackendMode = static_cast<int32>(Port->GetRuntimeMode());
		}
	}

	float HP = 0.0f, MaxHP = 0.0f;
	if (auto* BaseChar = Cast<AProject_JBaseCharacter>(Pawn))
	{
		if (auto* Attrs = BaseChar->GetAttributeSet())
		{
			HP = Attrs->GetHealth();
			MaxHP = Attrs->GetMaxHealth();
		}
	}

	FString MountInfo = TEXT("OnFoot");
	if (auto* MountPawn = Cast<AProject_JMountMoverPawn>(Pawn))
	{
		MountInfo = MountPawn->IsFlying() ? TEXT("Mount:Flying") : TEXT("Mount:Ground");
	}

	FString EncounterInfo = TEXT("None");
	if (World)
	{
		for (TActorIterator<AProject_JWorldEncounter> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				EncounterInfo = FString::Printf(TEXT("%s (Phase=%d)"), *It->EncounterId.ToString(), static_cast<int32>(It->Phase));
				break;
			}
		}
	}

	const FVector Loc = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const float Speed = Pawn ? static_cast<float>(Pawn->GetVelocity().Size()) : 0.0f;

	const FString Summary = FString::Printf(
		TEXT("Map: %s | NetMode: %d | Backend: %d\nPos: (%.0f, %.0f, %.0f) Spd: %.0f\nHP: %.0f/%.0f | %s\nEncounter: %s | Inbox: %d"),
		*MapName, NetMode, BackendMode,
		Loc.X, Loc.Y, Loc.Z, Speed,
		HP, MaxHP, *MountInfo,
		*EncounterInfo, LastInboxCount);
	TelemetryText->SetText(FText::FromString(Summary));
}

void UProject_JMMOTestHarnessWidget::AppendStatusMessage(const FString& Message)
{
	if (StatusLogText)
	{
		StatusLogText->SetText(FText::FromString(TEXT("Status: ") + Message));
	}
}

void UProject_JMMOTestHarnessWidget::HandleEnterDungeonClicked()
{
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerRequestDungeonEntryFromMenu(TEXT("Dungeon_Starter"));
		AppendStatusMessage(TEXT("Requested Dungeon_Starter entry..."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleReturnWorldClicked()
{
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerRequestDungeonReturn();
		AppendStatusMessage(TEXT("Requested Dungeon return..."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleRefreshMailClicked()
{
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerRequestMailInbox(FGuid());
		AppendStatusMessage(TEXT("Refreshing Mail Inbox..."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleClaimFirstMailClicked()
{
	if (!LastClaimableMailId.IsValid())
	{
		AppendStatusMessage(TEXT("No cached mail ID. Click Refresh Inbox first."));
		return;
	}
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerClaimMail(LastClaimableMailId);
		AppendStatusMessage(FString::Printf(TEXT("Claiming Mail %s..."), *LastClaimableMailId.ToString(EGuidFormats::Short)));
	}
}

void UProject_JMMOTestHarnessWidget::HandleHealFullClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (auto* Cheat = Cast<UProject_JCheatManager>(PC->CheatManager))
		{
			Cheat->PJ_HealFull();
			AppendStatusMessage(TEXT("Executed PJ_HealFull."));
			RefreshTelemetry();
		}
	}
}

void UProject_JMMOTestHarnessWidget::HandleKillSelfClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (auto* Cheat = Cast<UProject_JCheatManager>(PC->CheatManager))
		{
			Cheat->PJ_KillSelf();
			AppendStatusMessage(TEXT("Executed PJ_KillSelf."));
			RefreshTelemetry();
		}
	}
}

void UProject_JMMOTestHarnessWidget::HandleReviveGraveyardClicked()
{
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerRequestRevive(EProject_JRevivePath::Graveyard);
		AppendStatusMessage(TEXT("Requested Graveyard revive..."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleReviveInPlaceClicked()
{
	if (auto* Bridge = ResolveBridge())
	{
		Bridge->ServerRequestRevive(EProject_JRevivePath::ReviveItem);
		AppendStatusMessage(TEXT("Requested In-Place Item revive..."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleToggleFlightClicked()
{
	APlayerController* PC = GetOwningPlayer();
	if (auto* MountPawn = PC ? Cast<AProject_JMountMoverPawn>(PC->GetPawn()) : nullptr)
	{
		MountPawn->ServerRequestToggleFlight();
		AppendStatusMessage(TEXT("Toggled Mount Flight."));
	}
	else
	{
		AppendStatusMessage(TEXT("Not riding AProject_JMountMoverPawn."));
	}
}

void UProject_JMMOTestHarnessWidget::HandleCycleVFXOpacityClicked()
{
	static constexpr float Presets[] = { 1.0f, 0.35f, 0.0f };
	OpacityPresetIndex = (OpacityPresetIndex + 1) % 3;
	const float Value = Presets[OpacityPresetIndex];
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale")))
	{
		Var->Set(Value, ECVF_SetByGameSetting);
		AppendStatusMessage(FString::Printf(TEXT("OtherPlayer VFX Opacity -> %.2f"), Value));
	}
}

void UProject_JMMOTestHarnessWidget::HandleToggleBackendModeClicked()
{
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone)
	{
		AppendStatusMessage(TEXT("Backend mode can be changed in a Standalone test."));
		return;
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		auto* Port = GI ? GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>() : nullptr;
		if (auto* Cheat = Cast<UProject_JCheatManager>(PC->CheatManager); Cheat && Port)
		{
			const int32 NextMode = Port->GetRuntimeMode() == EProject_JBackendRuntimeMode::StandaloneMock ? 1 : 0;
			Cheat->PJ_SetBackendMode(NextMode);
			AppendStatusMessage(FString::Printf(TEXT("BackendMode -> %d"), NextMode));
			RefreshTelemetry();
		}
	}
}

void UProject_JMMOTestHarnessWidget::HandleCloseClicked()
{
	RemoveFromParent();
}

void UProject_JMMOTestHarnessWidget::HandleMMOActionResult(FName ActionName, bool bSucceeded, FName ReasonCode)
{
	if (bSucceeded && ActionName == TEXT("MailClaim"))
	{
		LastClaimableMailId.Invalidate();
		if (auto* Bridge = ResolveBridge()) Bridge->ServerRequestMailInbox({});
	}
	AppendStatusMessage(FString::Printf(TEXT("%s: %s (%s)"),
		*ActionName.ToString(),
		bSucceeded ? TEXT("OK") : TEXT("FAIL"),
		*ReasonCode.ToString()));
}

void UProject_JMMOTestHarnessWidget::HandleDungeonTicketReceived(const FProject_JDungeonJoinTicket& Ticket)
{
	AppendStatusMessage(FString::Printf(TEXT("Ticket Received: %s -> %s"),
		*Ticket.DungeonId.ToString(), *Ticket.ServerEndpoint));
}

void UProject_JMMOTestHarnessWidget::HandleMailInboxReceived(const TArray<FProject_JInboxEntry>& Entries, FGuid Next, bool bSucceeded)
{
	LastInboxCount = Entries.Num();
	LastClaimableMailId = !bSucceeded || Entries.IsEmpty() ? FGuid() : Entries[0].MailId;
	AppendStatusMessage(FString::Printf(TEXT("Inbox %s: %d entries"),
		bSucceeded ? TEXT("OK") : TEXT("FAIL"), Entries.Num()));
	RefreshTelemetry();
}
