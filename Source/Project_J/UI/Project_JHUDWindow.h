#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Project_JHUDWindow.generated.h"
class UProject_JPlayerHUDWidget;
class UVerticalBox;
class UBorder;
class UTextBlock;
UCLASS()
class PROJECT_J_API UProject_JUICommandButton : public UButton
{
	GENERATED_BODY()
  public:
	UProject_JUICommandButton();
	void InitializeCommand(UProject_JPlayerHUDWidget *InHUD, FName InCommand, const FText &Label);

  private:
	UFUNCTION() void Execute();
	TWeakObjectPtr<UProject_JPlayerHUDWidget> HUD;
	FName Command;
};
/** Movable independently skinnable menu. Owns no inventory/gameplay state. */
UCLASS(Blueprintable)
class PROJECT_J_API UProject_JHUDWindow : public UUserWidget
{
	GENERATED_BODY()
  public:
	void InitializeWindow(UProject_JPlayerHUDWidget *InHUD, FName InId, const FText &Title);
	void ApplyLayout(FVector2D Extent);
	void SetIdentity(const FString &Key);
	/** Bind a replacement skin's close button to this command. */
	UFUNCTION(BlueprintCallable) void RequestClose();
	FName GetWindowId() const
	{
		return WindowId;
	}
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> BodyBox;
	UPROPERTY(EditDefaultsOnly) FVector2D PreferredSize = FVector2D(340, 440);

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
	virtual void NativeOnDragDetected(const FGeometry &, const FPointerEvent &, UDragDropOperation *&) override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TitleLabel;

  private:
	TWeakObjectPtr<UProject_JPlayerHUDWidget> HUD;
	FString LayoutKey;
	FName WindowId;
	FText Title;
	FVector2D NormalizedPosition = FVector2D(0.35, 0.25);
	FVector2D DragOffset;

  public:
	bool FinishMove(const FPointerEvent &Event);
};
