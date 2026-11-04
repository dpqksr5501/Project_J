#include "UI/Project_JCraftingJobsWidget.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
UTextBlock* JobText(UWidgetTree* Tree, FName Name, const FText& Text, int32 Size = 12)
{
	auto* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Label->SetText(Text);
	Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", Size));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(.88f, .92f, .96f)));
	Label->SetAutoWrapText(true);
	return Label;
}
UButton* JobButton(UWidgetTree* Tree, FName Name, const FText& Text)
{
	auto* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	auto Style = Button->GetStyle();
	Style.Normal = *FCoreStyle::Get().GetBrush("WhiteBrush");
	Style.Normal.TintColor = FSlateColor(FLinearColor(.06f, .10f, .14f));
	Style.Hovered = Style.Normal;
	Style.Hovered.TintColor = FSlateColor(FLinearColor(.10f, .18f, .24f));
	Style.Pressed = Style.Normal;
	Style.NormalPadding = FMargin(7, 4);
	Button->SetStyle(Style);
	Button->AddChild(JobText(Tree, NAME_None, Text));
	return Button;
}
FText RequestFeedback(const FProject_JCraftingRequestState& State)
{
	switch (State.Stage)
	{
	case EProject_JCraftingRequestStage::Pending:
		return State.Action == EProject_JCraftingJobAction::Collect
			? NSLOCTEXT("ProjectJUI", "CraftCollectPending", "수령을 확인하는 중…")
			: NSLOCTEXT("ProjectJUI", "CraftCancelPending", "취소를 확인하는 중…");
	case EProject_JCraftingRequestStage::Rejected:
		return NSLOCTEXT("ProjectJUI", "CraftRejected", "요청을 수락하지 못했습니다. 작업 상태를 확인하세요.");
	case EProject_JCraftingRequestStage::Unknown:
		return NSLOCTEXT("ProjectJUI", "CraftUnknown", "처리 결과를 확인하지 못했습니다. 작업 상태를 확인한 뒤 다시 시도하세요.");
	case EProject_JCraftingRequestStage::Confirmed:
		return State.Action == EProject_JCraftingJobAction::Collect
			? NSLOCTEXT("ProjectJUI", "CraftCollected", "제작품을 수령했습니다.")
			: NSLOCTEXT("ProjectJUI", "CraftCancelled", "제작을 취소했습니다.");
	default: return FText::GetEmpty();
	}
}
}

TSharedRef<SWidget> UProject_JCraftingJobRow::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrushColor(FLinearColor(.025f, .04f, .06f, .96f));
		Border->SetPadding(FMargin(8));
		WidgetTree->RootWidget = Border;
		auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Border->AddChild(Column);
		JobLabel = JobText(WidgetTree, TEXT("JobLabel"), FText::GetEmpty());
		Column->AddChildToVerticalBox(JobLabel)->SetPadding(FMargin(0, 0, 0, 6));
		auto* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Column->AddChildToVerticalBox(Actions);
		TimeLabel = JobText(WidgetTree, TEXT("TimeLabel"), FText::GetEmpty(), 11);
		Actions->AddChildToHorizontalBox(TimeLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CollectButton = JobButton(WidgetTree, TEXT("CollectButton"), NSLOCTEXT("ProjectJUI", "CraftCollect", "수령"));
		CancelButton = JobButton(WidgetTree, TEXT("CancelButton"), NSLOCTEXT("ProjectJUI", "CraftCancel", "취소"));
		Actions->AddChildToHorizontalBox(CollectButton)->SetPadding(FMargin(4, 0));
		Actions->AddChildToHorizontalBox(CancelButton);
	}
	return Super::RebuildWidget();
}
void UProject_JCraftingJobRow::NativeConstruct()
{
	Super::NativeConstruct();
	if (CollectButton) CollectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Collect);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::Cancel);
}
void UProject_JCraftingJobRow::NativeDestruct()
{
	Retire();
	if (CollectButton) CollectButton->OnClicked.RemoveDynamic(this, &ThisClass::Collect);
	if (CancelButton) CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::Cancel);
	Super::NativeDestruct();
}
void UProject_JCraftingJobRow::Retire()
{
	OwnerUI.Reset(); ExpectedSource.Reset(); JobId.Invalidate(); OwnerCharacterId.Invalidate(); OwnerGeneration = 0;
}
void UProject_JCraftingJobRow::Configure(UProject_JPlayerUIComponent* UI, UProject_JCraftingJobsComponent* Source,
	const FProject_JCraftingJobSummary& Summary)
{
	OwnerUI = UI; ExpectedSource = Source; JobId = Summary.JobId; OwnerCharacterId = Summary.OwnerCharacterId;
	OwnerGeneration = Source ? Source->GetPresentationGeneration() : 0;
	if (JobLabel && LastBatches != Summary.Batches)
	{
		LastBatches = Summary.Batches;
		JobLabel->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "CraftJobBatches", "제작 작업 ×{0}"), FText::AsNumber(Summary.Batches)));
	}
}
void UProject_JCraftingJobRow::Refresh(double Remaining, bool bRequestPending)
{
	const TStrongObjectPtr<UProject_JCraftingJobRow> KeepSelf(this);
	const bool bCurrent = OwnerUI.IsValid() && ExpectedSource.IsValid() && JobId.IsValid()
		&& OwnerUI->GetCraftingJobsComponent() == ExpectedSource.Get()
		&& OwnerGeneration != 0 && ExpectedSource->GetPresentationGeneration() == OwnerGeneration;
	const int64 Seconds = bCurrent && FMath::IsFinite(Remaining) && Remaining >= 0
		? FMath::CeilToInt64(FMath::Min(Remaining, double(MAX_int32))) : -1;
	if (TimeLabel && LastRemaining != Seconds)
	{
		LastRemaining = Seconds;
		TimeLabel->SetText(Seconds < 0 ? NSLOCTEXT("ProjectJUI", "CraftTimeUnknown", "시간 확인 중")
			: Seconds == 0 ? NSLOCTEXT("ProjectJUI", "CraftReady", "수령 가능")
			: FText::Format(NSLOCTEXT("ProjectJUI", "CraftSeconds", "남은 시간: {0}초"), FText::AsNumber(Seconds)));
	}
	const bool bCanCancel = bCurrent && !bRequestPending;
	const bool bCanCollect = bCanCancel && Seconds == 0;
	if (CollectButton && CollectButton->GetIsEnabled() != bCanCollect) CollectButton->SetIsEnabled(bCanCollect);
	// Changing focus may reenter the source binding. Re-evaluate before the second button.
	const bool bStillCurrent = bCanCancel && OwnerUI.IsValid() && ExpectedSource.IsValid()
		&& OwnerUI->GetCraftingJobsComponent() == ExpectedSource.Get()
		&& OwnerGeneration != 0 && ExpectedSource->GetPresentationGeneration() == OwnerGeneration;
	if (CancelButton && CancelButton->GetIsEnabled() != bStillCurrent) CancelButton->SetIsEnabled(bStillCurrent);
}
void UProject_JCraftingJobRow::Request(EProject_JCraftingJobAction Action)
{
	const TStrongObjectPtr<UProject_JCraftingJobRow> KeepSelf(this);
	const TStrongObjectPtr<UProject_JPlayerUIComponent> UI(OwnerUI.Get());
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> Source(ExpectedSource.Get());
	if (UI.IsValid() && Source.IsValid() && JobId.IsValid()) UI->RequestCraftingJobAction(Source.Get(), OwnerCharacterId, OwnerGeneration, JobId, Action);
}
void UProject_JCraftingJobRow::Collect() { Request(EProject_JCraftingJobAction::Collect); }
void UProject_JCraftingJobRow::Cancel() { Request(EProject_JCraftingJobAction::Cancel); }

TSharedRef<SWidget> UProject_JCraftingJobsWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Column;
		RequestLabel = JobText(WidgetTree, TEXT("RequestLabel"), FText::GetEmpty(), 11);
		Column->AddChildToVerticalBox(RequestLabel)->SetPadding(FMargin(0, 0, 0, 6));
		EmptyLabel = JobText(WidgetTree, TEXT("EmptyLabel"), NSLOCTEXT("ProjectJUI", "CraftConnecting", "제작 작업 정보를 기다리는 중입니다."));
		Column->AddChildToVerticalBox(EmptyLabel);
		RowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowsBox"));
		auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Scroll->AddChild(RowsBox);
	}
	return Super::RebuildWidget();
}
void UProject_JCraftingJobsWidget::DetachRows()
{
	// Detach ownership before widget callbacks; never clear children belonging to a reentrant newer view.
	auto Detached = MoveTemp(Rows); Rows.Reset();
	OwnerUI.Reset(); ExpectedSource.Reset(); ExpectedGeneration = 0; LastRequest = {}; LastFeedback = FText::GetEmpty();
	for (auto& Pair : Detached)
	{
		const TStrongObjectPtr<UProject_JCraftingJobRow> Row(Pair.Value.Get());
		if (Row.IsValid()) { Row->Retire(); Row->RemoveFromParent(); }
	}
}
void UProject_JCraftingJobsWidget::NativeDestruct()
{
	++PresentationRevision; DetachRows();
	Super::NativeDestruct();
}
bool UProject_JCraftingJobsWidget::IsCurrent(uint64 Revision, const UProject_JCraftingJobsComponent* Source) const
{
	return Revision == PresentationRevision && ExpectedSource.Get() == Source && Source && OwnerUI.IsValid()
		&& OwnerUI->GetCraftingJobsComponent() == Source
		&& ExpectedGeneration != 0 && Source->GetPresentationGeneration() == ExpectedGeneration;
}
void UProject_JCraftingJobsWidget::Present(UProject_JPlayerUIComponent* UI, UProject_JCraftingJobsComponent* Source)
{
	const TStrongObjectPtr<UProject_JCraftingJobsWidget> KeepSelf(this);
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepUI(UI);
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepSource(Source);
	const uint64 Revision = ++PresentationRevision;
	if (!UI || UI->GetCraftingJobsComponent() != Source) Source = nullptr;
	const uint64 Generation = Source ? Source->GetPresentationGeneration() : 0;
	if (OwnerUI.Get() != UI || ExpectedSource.Get() != Source || ExpectedGeneration != Generation || !Source)
	{
		DetachRows();
		if (Revision != PresentationRevision) return;
		OwnerUI = UI; ExpectedSource = Source; ExpectedGeneration = Generation;
	}
	const auto Summaries = Source ? Source->GetJobs() : TArray<FProject_JCraftingJobSummary>();
	TSet<FGuid> Ids;
	bool bValid = Summaries.Num() <= 8 && (!Source || Generation != 0);
	if (bValid) for (const auto& Summary : Summaries)
	{
		bValid &= Summary.JobId.IsValid() && Summary.OwnerCharacterId.IsValid()
			&& Summary.OwnerGeneration == Generation && !Ids.Contains(Summary.JobId);
		Ids.Add(Summary.JobId);
	}
	if (!bValid)
	{
		DetachRows();
		if (Revision != PresentationRevision) return;
		Source = nullptr;
	}
	TArray<FGuid, TInlineAllocator<8>> Removed;
	for (const auto& Pair : Rows) if (!Ids.Contains(Pair.Key)) Removed.Add(Pair.Key);
	for (const FGuid& Id : Removed)
	{
		const TStrongObjectPtr<UProject_JCraftingJobRow> Row(Rows.FindRef(Id).Get());
		Rows.Remove(Id);
		if (Row.IsValid()) { Row->Retire(); Row->RemoveFromParent(); }
		if (Revision != PresentationRevision) return;
	}
	if (Source && RowsBox)
	{
		for (const auto& Summary : Summaries)
		{
			if (!IsCurrent(Revision, Source)) return;
			auto* Existing = Rows.FindRef(Summary.JobId).Get();
			if (!Existing)
			{
				const TStrongObjectPtr<UProject_JCraftingJobRow> Created(CreateWidget<UProject_JCraftingJobRow>(GetOwningPlayer(), RowClass ? RowClass.Get() : UProject_JCraftingJobRow::StaticClass()));
				if (!Created.IsValid() || !IsCurrent(Revision, Source)) return;
				// Native/BP Construct may supersede this view. Publish the row only after that callback.
				const TSharedRef<SWidget> Built = Created->TakeWidget();
				if (!IsCurrent(Revision, Source)) { Created->Retire(); return; }
				Rows.Add(Summary.JobId, Created.Get());
				RowsBox->AddChildToVerticalBox(Created.Get())->SetPadding(FMargin(0, 0, 0, 6));
				if (!IsCurrent(Revision, Source)) return;
				Existing = Created.Get();
			}
			Existing->Configure(UI, Source, Summary);
		}
	}
	if (Revision != PresentationRevision) return;
	if (EmptyLabel)
	{
		EmptyLabel->SetText(Source ? NSLOCTEXT("ProjectJUI", "CraftEmpty", "표시할 제작 작업이 없습니다.")
			: NSLOCTEXT("ProjectJUI", "CraftConnecting", "제작 작업 정보를 기다리는 중입니다."));
		EmptyLabel->SetVisibility(Rows.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!Source) { if (RequestLabel) RequestLabel->SetText(FText::GetEmpty()); return; }
	RefreshValues(Revision);
}
void UProject_JCraftingJobsWidget::RefreshValues(uint64 Revision)
{
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> Source(ExpectedSource.Get());
	if (!IsCurrent(Revision, Source.Get())) return;
	LastRequest = Source->GetRequestState();
	const FText Feedback = RequestFeedback(LastRequest);
	if (RequestLabel && !Feedback.EqualTo(LastFeedback)) { LastFeedback = Feedback; RequestLabel->SetText(Feedback); }
	TArray<TStrongObjectPtr<UProject_JCraftingJobRow>, TInlineAllocator<8>> ObservedRows;
	TArray<FGuid, TInlineAllocator<8>> ObservedIds;
	for (const auto& Pair : Rows) { ObservedRows.Emplace(Pair.Value.Get()); ObservedIds.Add(Pair.Key); }
	for (int32 Index = 0; Index < ObservedRows.Num(); ++Index)
	{
		if (!IsCurrent(Revision, Source.Get())) return;
		if (ObservedRows[Index].IsValid()) ObservedRows[Index]->Refresh(Source->GetRemainingSeconds(ObservedIds[Index]), LastRequest.Stage == EProject_JCraftingRequestStage::Pending);
	}
}
void UProject_JCraftingJobsWidget::RefreshClock()
{
	const TStrongObjectPtr<UProject_JCraftingJobsWidget> KeepSelf(this);
	RefreshValues(++PresentationRevision);
}
bool UProject_JCraftingJobsWidget::NeedsClockRefresh() const
{
	return !Rows.IsEmpty() || LastRequest.Stage == EProject_JCraftingRequestStage::Pending;
}
