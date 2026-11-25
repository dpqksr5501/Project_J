#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Project_JMMOTestHarnessWidget.generated.h"

class UTextBlock;
class UButton;

/**
 * Self-contained programmatic UMG test harness overlay for rapid PIE / QA iteration
 * across Dungeon, Mail, Revive, Field Boss, Mount Flight, VFX Opacity, and Backend Mode.
 * Toggleable via `PJ_ToggleTestUI` cheat command; requires zero manual WBP wiring,
 * and can also be subclassed in Blueprint for custom layout.
 */
UCLASS(BlueprintType, Blueprintable)
class PROJECT_J_API UProject_JMMOTestHarnessWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MMO|TestUI")
	void RefreshTelemetry();

	UFUNCTION(BlueprintCallable, Category = "MMO|TestUI")
	void AppendStatusMessage(const FString& Message);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UProject_JMMOClientBridgeComponent* ResolveBridge() const;
	void BindBridgeDelegates();
	void UnbindBridgeDelegates();

	UFUNCTION() void HandleEnterDungeonClicked();
	UFUNCTION() void HandleReturnWorldClicked();
	UFUNCTION() void HandleRefreshMailClicked();
	UFUNCTION() void HandleClaimFirstMailClicked();
	UFUNCTION() void HandleHealFullClicked();
	UFUNCTION() void HandleKillSelfClicked();
	UFUNCTION() void HandleReviveGraveyardClicked();
	UFUNCTION() void HandleReviveInPlaceClicked();
	UFUNCTION() void HandleToggleFlightClicked();
	UFUNCTION() void HandleCycleVFXOpacityClicked();
	UFUNCTION() void HandleToggleBackendModeClicked();
	UFUNCTION() void HandleCloseClicked();

	UFUNCTION() void HandleMMOActionResult(FName ActionName, bool bSucceeded, FName ReasonCode);
	UFUNCTION() void HandleDungeonTicketReceived(const FProject_JDungeonJoinTicket& Ticket);
	UFUNCTION() void HandleMailInboxReceived(const TArray<FProject_JInboxEntry>& Entries, FGuid Next, bool bSucceeded);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> TelemetryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusLogText;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> ActionButtons;

	TWeakObjectPtr<UProject_JMMOClientBridgeComponent> BoundBridge;
	FGuid LastClaimableMailId;
	int32 LastInboxCount = 0;
	float TelemetryAccumulator = 0.0f;
	int32 OpacityPresetIndex = 0;
	bool bOwnsCursorMode = false;
	bool bOwnsFallbackInput = false;
	bool bPreviousMouseCursor = false;
};
