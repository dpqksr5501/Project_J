#include "UI/Project_JUIWindowStackSubsystem.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProjectJLoginQueue"

void UProject_JUIWindowStackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	OnlineWorld = Collection.InitializeDependency<UProject_JOnlineWorldSubsystem>();
	if (auto* Online = OnlineWorld.Get())
	{
		Online->OnLoginQueued.AddUniqueDynamic(this, &ThisClass::OnLoginQueued);
		Online->OnConnectDispatched.AddUniqueDynamic(this, &ThisClass::OnConnectDispatched);
		Online->OnLoginQueueClosed.AddUObject(this, &ThisClass::DismissLoginQueue);
		OnLoginQueued(Online->GetQueuedLoginPosition());
	}
	LocalPlayerRemovedHandle = GetGameInstance()->OnLocalPlayerRemovedEvent.AddUObject(this, &ThisClass::OnLocalPlayerRemoved);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}

void UProject_JUIWindowStackSubsystem::Deinitialize()
{
	if (auto* Online = OnlineWorld.Get())
	{
		Online->OnLoginQueued.RemoveAll(this);
		Online->OnConnectDispatched.RemoveAll(this);
		Online->OnLoginQueueClosed.RemoveAll(this);
	}
	OnlineWorld.Reset();
	GetGameInstance()->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
	LocalPlayerRemovedHandle.Reset();
	DismissLoginQueue();
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
	WindowStack.Reset();
	Super::Deinitialize();
}

void UProject_JUIWindowStackSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		WindowStack.Reset();
		if (auto* Online = OnlineWorld.Get())
			if (auto* Controller = Online->GetQueuedLoginController(); Controller && Controller->GetWorld() == World)
				Online->CancelPendingLogin();
		if (!LoginQueueController.IsValid() || LoginQueueController->GetWorld() == World) DismissLoginQueue();
	}
}

void UProject_JUIWindowStackSubsystem::OnLocalPlayerRemoved(ULocalPlayer* Player)
{
	if (LoginQueuePlayer.Get() == Player) CancelLoginQueue();
}

void UProject_JUIWindowStackSubsystem::OnLoginQueueControllerChanged(APlayerController* NewController)
{
	if (NewController != LoginQueueController.Get()) CancelLoginQueue();
}

void UProject_JUIWindowStackSubsystem::OnLoginQueued(int32 Position)
{
	auto* Online = OnlineWorld.Get();
	if (!Online || Position <= 0 || Online->GetQueuedLoginPosition() != Position) return;
	auto* Controller = Online->GetQueuedLoginController();
	auto* Player = IsValid(Controller) && !Controller->IsActorBeingDestroyed() ? Controller->GetLocalPlayer() : nullptr;
	auto* Viewport = Player ? Player->ViewportClient.Get() : nullptr;
	if (!Player || Player->GetGameInstance() != GetGameInstance() || Player->GetPlayerController(Controller->GetWorld()) != Controller
		|| !Viewport || !Viewport->GetGameLayerManager().IsValid() || !Viewport->GetGameViewportWidget().IsValid()
		|| !FSlateApplication::IsInitialized()) return;

	const FText QueueText = FText::Format(LOCTEXT("Position", "월드 접속 대기 · {0}번"), FText::AsNumber(Position));
	if (LoginQueueController.Get() == Controller && LoginQueuePlayer.Get() == Player && LoginQueueViewport.Get() == Viewport && LoginQueueLabel)
	{
		LoginQueueLabel->SetText(QueueText);
		return;
	}
	DismissLoginQueue();

	TSharedPtr<SButton> Cancel;
	LoginQueueOverlay = SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 36))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.025f, 0.036f, 0.052f, 0.98f))
			.Padding(FMargin(24, 16))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SAssignNew(LoginQueueLabel, STextBlock).Text(QueueText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
				[
					SAssignNew(Cancel, SButton).Text(LOCTEXT("Cancel", "대기 취소"))
					.OnClicked(FOnClicked::CreateUObject(this, &ThisClass::CancelLoginQueue))
				]
			]
		];
	LoginQueueController = Controller;
	LoginQueuePlayer = Player;
	LoginQueueViewport = Viewport;
	LoginQueueCancelButton = Cancel;
	ControllerChangedHandle = Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::OnLoginQueueControllerChanged);
	if (const auto SlateUser = Player->GetSlateUser())
	{
		LoginQueueSlateUser = SlateUser->GetUserIndex();
		PriorLoginQueueFocus = FSlateApplication::Get().GetUserFocusedWidget(LoginQueueSlateUser);
	}
	PriorMouseCapture = Viewport->GetMouseCaptureMode();
	PriorMouseLock = Viewport->GetMouseLockMode();
	bPriorViewportIgnoresInput = Viewport->IgnoreInput();
	bPriorHideCursorDuringCapture = Viewport->HideCursorDuringCapture();
	bPriorShowMouseCursor = Controller->bShowMouseCursor;
	Viewport->AddViewportWidgetForPlayer(Player, LoginQueueOverlay.ToSharedRef(), 150);

	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(Cancel);
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Controller->bShowMouseCursor = true;
	Controller->SetInputMode(Mode);
}

void UProject_JUIWindowStackSubsystem::OnConnectDispatched(bool)
{
	DismissLoginQueue();
}

FReply UProject_JUIWindowStackSubsystem::CancelLoginQueue()
{
	if (auto* Online = OnlineWorld.Get(); Online && LoginQueueController.IsValid()
		&& Online->GetQueuedLoginController() == LoginQueueController.Get())
	{
		Online->CancelPendingLogin();
	}
	DismissLoginQueue();
	return FReply::Handled();
}

bool UProject_JUIWindowStackSubsystem::DeferHUDInputModeForLoginQueue(APlayerController* Controller)
{
	if (!LoginQueueOverlay.IsValid() || LoginQueueController.Get() != Controller) return false;
	bDeferredHUDInputMode = true;
	return true;
}

void UProject_JUIWindowStackSubsystem::DismissLoginQueue()
{
	if (!LoginQueueOverlay.IsValid()) return;
	const auto Overlay = MoveTemp(LoginQueueOverlay);
	const auto CancelButton = MoveTemp(LoginQueueCancelButton);
	LoginQueueLabel.Reset();
	const auto Controller = LoginQueueController;
	const auto Player = LoginQueuePlayer;
	const auto Viewport = LoginQueueViewport;
	LoginQueueController.Reset();
	LoginQueuePlayer.Reset();
	LoginQueueViewport.Reset();
	const bool bRefreshHUDInput = bDeferredHUDInputMode;
	bDeferredHUDInputMode = false;
	if (auto* Local = Player.Get()) Local->OnPlayerControllerChanged().Remove(ControllerChangedHandle);
	ControllerChangedHandle.Reset();

	if (FSlateApplication::IsInitialized() && LoginQueueSlateUser != INDEX_NONE)
	{
		auto& Slate = FSlateApplication::Get();
		if (Slate.GetUserFocusedWidget(LoginQueueSlateUser) == CancelButton)
		{
			if (const auto PriorFocus = PriorLoginQueueFocus.Pin()) Slate.SetUserFocus(LoginQueueSlateUser, PriorFocus);
			else Slate.ClearUserFocus(LoginQueueSlateUser);
		}
	}
	LoginQueueSlateUser = INDEX_NONE;
	PriorLoginQueueFocus.Reset();
	bool bOwnedViewportInput = false;
	if (auto* GameViewport = Viewport.Get())
	{
		if (auto* Local = Player.Get()) GameViewport->RemoveViewportWidgetForPlayer(Local, Overlay.ToSharedRef());
		// Preserve an input mode explicitly installed by a different screen after the queue opened.
		bOwnedViewportInput = GameViewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown
			&& GameViewport->GetMouseLockMode() == EMouseLockMode::DoNotLock && !GameViewport->HideCursorDuringCapture() && !GameViewport->IgnoreInput();
		if (bOwnedViewportInput)
		{
			GameViewport->SetMouseCaptureMode(PriorMouseCapture);
			GameViewport->SetMouseLockMode(PriorMouseLock);
			GameViewport->SetHideCursorDuringCapture(bPriorHideCursorDuringCapture);
			GameViewport->SetIgnoreInput(bPriorViewportIgnoresInput);
		}
	}
	if (auto* PC = Controller.Get(); PC && PC->GetLocalPlayer() == Player.Get())
	{
		if (bOwnedViewportInput && PC->bShowMouseCursor) PC->bShowMouseCursor = bPriorShowMouseCursor;
		if (auto* UI = bOwnedViewportInput && bRefreshHUDInput && !PC->IsActorBeingDestroyed()
			? PC->FindComponentByClass<UProject_JPlayerUIComponent>() : nullptr;
			UI && !UI->bEnding && UI->Screen)
			UI->ApplyInputMode();
	}
}

bool UProject_JUIWindowStackSubsystem::PushModalWindow(FName WindowId)
{
	if (WindowId.IsNone())
	{
		return false;
	}
	WindowStack.Remove(WindowId); // Bring to top if already open.
	WindowStack.Add(WindowId);
	return true;
}

FName UProject_JUIWindowStackSubsystem::HandleEscapeCloseTop()
{
	if (WindowStack.IsEmpty())
	{
		return NAME_None;
	}
	return WindowStack.Pop();
}

bool UProject_JUIWindowStackSubsystem::CloseWindow(FName WindowId)
{
	return WindowStack.Remove(WindowId) > 0;
}

void UProject_JUIWindowStackSubsystem::ApplyAccessibilitySettings(const FProject_JAccessibilitySettings& NewSettings)
{
	Accessibility = NewSettings;
	Accessibility.UIScale = FMath::Clamp(Accessibility.UIScale, 0.8f, 1.4f);
	Accessibility.OtherPlayerVFXOpacity = FMath::Clamp(Accessibility.OtherPlayerVFXOpacity, 0.0f, 1.0f);
}

#undef LOCTEXT_NAMESPACE
