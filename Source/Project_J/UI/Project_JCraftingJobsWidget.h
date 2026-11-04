#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Project_JCraftingJobsWidget.generated.h"

class UProject_JPlayerUIComponent;
class UTextBlock;
class UButton;
class UVerticalBox;

/** One bounded view row. An old row can never redirect its click to a new character/source. */
UCLASS()
class PROJECT_J_API UProject_JCraftingJobRow : public UUserWidget
{
	GENERATED_BODY()
public:
	void Configure(UProject_JPlayerUIComponent* UI, UProject_JCraftingJobsComponent* Source,
		const FProject_JCraftingJobSummary& Summary);
	void Refresh(double Remaining, bool bRequestPending);
	void Retire();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> JobLabel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TimeLabel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CollectButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CancelButton;
private:
	UFUNCTION() void Collect();
	UFUNCTION() void Cancel();
	void Request(EProject_JCraftingJobAction Action);
	TWeakObjectPtr<UProject_JPlayerUIComponent> OwnerUI;
	TWeakObjectPtr<UProject_JCraftingJobsComponent> ExpectedSource;
	FGuid JobId, OwnerCharacterId;
	uint64 OwnerGeneration = 0;
	int32 LastBatches = INDEX_NONE;
	int64 LastRemaining = MIN_int64;
};

/** Data events reconcile at most eight stable rows; the HUD supplies the visible one-second clock. */
UCLASS()
class PROJECT_J_API UProject_JCraftingJobsWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void Present(UProject_JPlayerUIComponent* UI, UProject_JCraftingJobsComponent* Source);
	void RefreshClock();
	bool NeedsClockRefresh() const;
	int32 GetPresentedJobCount() const { return Rows.Num(); }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> RowsBox;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EmptyLabel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RequestLabel;
	UPROPERTY(EditDefaultsOnly, Category="UI|Skin") TSubclassOf<UProject_JCraftingJobRow> RowClass;
private:
	void DetachRows();
	bool IsCurrent(uint64 Revision, const UProject_JCraftingJobsComponent* Source) const;
	void RefreshValues(uint64 Revision);
	UPROPERTY() TMap<FGuid, TObjectPtr<UProject_JCraftingJobRow>> Rows;
	TWeakObjectPtr<UProject_JPlayerUIComponent> OwnerUI;
	TWeakObjectPtr<UProject_JCraftingJobsComponent> ExpectedSource;
	FProject_JCraftingRequestState LastRequest;
	FText LastFeedback;
	uint64 PresentationRevision = 0;
	uint64 ExpectedGeneration = 0;
};
