#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JUIWindowStackSubsystem.generated.h"

class APlayerController;
class FReply;
class SWidget;
class STextBlock;
class UGameViewportClient;
class ULocalPlayer;
class UProject_JOnlineWorldSubsystem;

UENUM(BlueprintType)
enum class EProject_JColorblindTelegraphMode : uint8
{
	DefaultRed,
	DeuteranopiaBlueYellow,
	ProtanopiaCyanOrange,
	HighContrastPattern
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JAccessibilitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility", meta = (ClampMin = "0.8", ClampMax = "1.4"))
	float UIScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	EProject_JColorblindTelegraphMode TelegraphPalette = EProject_JColorblindTelegraphMode::DefaultRed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OtherPlayerVFXOpacity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accessibility")
	bool bEnableCameraShake = true;
};

/**
 * Minimal UI Modal Stack, ESC Routing, Gamepad/Mouse Input Mode & Accessibility Manager (Stage A / BP06).
 * Prevents UI input-mode leaks across Inventory, Dungeon Menu, Auction House, Mail, and Guild windows.
 */
UCLASS()
class PROJECT_J_API UProject_JUIWindowStackSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "UI|Stack")
	bool PushModalWindow(FName WindowId);

	/** Pops the top-most window (ESC behavior); returns the closed WindowId or NAME_None if stack empty. */
	UFUNCTION(BlueprintCallable, Category = "UI|Stack")
	FName HandleEscapeCloseTop();

	UFUNCTION(BlueprintCallable, Category = "UI|Stack")
	bool CloseWindow(FName WindowId);

	UFUNCTION(BlueprintPure, Category = "UI|Stack")
	bool HasOpenModalWindow() const { return !WindowStack.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "UI|Stack")
	FName GetTopWindow() const { return WindowStack.IsEmpty() ? NAME_None : WindowStack.Last(); }

	UFUNCTION(BlueprintCallable, Category = "UI|Input")
	void SetUsingGamepad(bool bInUsingGamepad) { bUsingGamepad = bInUsingGamepad; }

	UFUNCTION(BlueprintPure, Category = "UI|Input")
	bool IsUsingGamepad() const { return bUsingGamepad; }

	UFUNCTION(BlueprintCallable, Category = "UI|Accessibility")
	void ApplyAccessibilitySettings(const FProject_JAccessibilitySettings& NewSettings);

	UFUNCTION(BlueprintPure, Category = "UI|Accessibility")
	FProject_JAccessibilitySettings GetAccessibilitySettings() const { return Accessibility; }

	/** An active queue overlay owns focus for its local controller until the request closes. */
	bool DeferHUDInputModeForLoginQueue(APlayerController* Controller);

private:
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void OnLocalPlayerRemoved(ULocalPlayer* Player);
	void OnLoginQueueControllerChanged(APlayerController* NewController);
	UFUNCTION() void OnLoginQueued(int32 Position);
	UFUNCTION() void OnConnectDispatched(bool bDispatched);
	void DismissLoginQueue();
	FReply CancelLoginQueue();
	TWeakObjectPtr<UProject_JOnlineWorldSubsystem> OnlineWorld;
	TWeakObjectPtr<APlayerController> LoginQueueController;
	TWeakObjectPtr<ULocalPlayer> LoginQueuePlayer;
	TWeakObjectPtr<UGameViewportClient> LoginQueueViewport;
	TSharedPtr<SWidget> LoginQueueOverlay;
	TSharedPtr<SWidget> LoginQueueCancelButton;
	TSharedPtr<STextBlock> LoginQueueLabel;
	TWeakPtr<SWidget> PriorLoginQueueFocus;
	int32 LoginQueueSlateUser = INDEX_NONE;
	EMouseCaptureMode PriorMouseCapture{};
	EMouseLockMode PriorMouseLock{};
	bool bPriorViewportIgnoresInput = false;
	bool bPriorHideCursorDuringCapture = false;
	bool bPriorShowMouseCursor = false;
	bool bDeferredHUDInputMode = false;
	FDelegateHandle ControllerChangedHandle;
	FDelegateHandle LocalPlayerRemovedHandle;
	FDelegateHandle WorldCleanupHandle;
	TArray<FName> WindowStack;
	FProject_JAccessibilitySettings Accessibility;
	bool bUsingGamepad = false;
};
