#include "Authoring/Project_JContentBundleAuthoring.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ProjectJContentBundle"

namespace ProjectJ::ContentAuthoring
{
class SContentBundleWindow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SContentBundleWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Request.Reset(NewObject<UProject_JContentBundleRequest>());
		FDetailsViewArgs DetailsArgs;
		DetailsArgs.bAllowSearch = false;
		DetailsArgs.bHideSelectionTip = true;
		DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
		Details->SetObject(Request.Get());
		Details->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
		{
			bPreviewValid = false;
			Registration.Reset();
			SetResult(TEXT("Settings changed. Preview and validate again before creating."));
		});
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(12)
			[
				SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Explanation", "Create a class or advancement with its own style and combo. Attacks, abilities and animation profiles stay shared. Preview first; creation does not save files or register the class for gameplay. Whole-bundle creation is not undoable; use Content Browser deletion to discard created assets."))
			]
			+ SVerticalBox::Slot().FillHeight(1).Padding(12, 0)[Details.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().Padding(12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[SNew(SButton).Text(LOCTEXT("Preview", "Preview & Validate")).OnClicked(this, &SContentBundleWindow::Preview)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[SNew(SButton).Text(LOCTEXT("Create", "Create Unsaved Assets")).IsEnabled_Lambda([this] { return bPreviewValid; }).OnClicked(this, &SContentBundleWindow::Create)]
				+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("Copy", "Copy Registry Entry")).IsEnabled_Lambda([this] { return !Registration.IsEmpty(); }).OnClicked_Lambda([this]
				{
					FPlatformApplicationMisc::ClipboardCopy(*Registration);
					return FReply::Handled();
				})]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 12)
			[
				SNew(SBox).HeightOverride(180)
				[SAssignNew(Output, SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true)
				.Text(LOCTEXT("Initial", "Choose an existing class template and a new identifier, then preview."))]
			]
		];
	}

private:
	void SetResult(const FString& Text) { if (Output) Output->SetText(FText::FromString(Text)); }
	void ShowErrors(const TArray<FText>& Errors)
	{
		FString Text = TEXT("Nothing created:\n");
		for (const FText& Error : Errors) Text += TEXT("- ") + Error.ToString() + TEXT("\n");
		SetResult(Text);
	}
	FReply Preview()
	{
		FBundlePlan Plan;
		TArray<FText> Errors;
		Registration.Reset();
		bPreviewValid = ValidateBundle(*Request, Plan, Errors);
		if (!bPreviewValid) ShowErrors(Errors);
		else
		{
			FString Text = TEXT("Validated. These new unsaved assets will be created:\n");
			for (const FString& Package : Plan.GetPackages()) Text += Package + TEXT("\n");
			Text += TEXT("\nSource assets stay unchanged. New root/style/optional combo references are connected automatically. Existing gameplay tags are reused; review identity tags and shared references after creation.");
			SetResult(Text);
		}
		return FReply::Handled();
	}
	FReply Create()
	{
		TArray<UObject*> Assets;
		TArray<FText> Errors;
		bPreviewValid = false;
		if (!CreateUnsavedBundle(*Request, Assets, Registration, Errors)) ShowErrors(Errors);
		else
		{
			if (GEditor) GEditor->SyncBrowserToObjects(Assets);
			SetResult(TEXT("Created and selected in Content Browser. Review and use the standard Save command when ready.\n\nAfter saving, add this entry to Config/DefaultGame.ini under:\n[/Script/Project_JCharacter.Project_JCharacterDataSubsystem]\n") + Registration +
				TEXT("\n\nFor advancements, review RequiredLevel, prerequisites, ExclusiveBranch and equipped-style override policy before use."));
		}
		return FReply::Handled();
	}
	TStrongObjectPtr<UProject_JContentBundleRequest> Request;
	TSharedPtr<IDetailsView> Details;
	TSharedPtr<SMultiLineEditableTextBox> Output;
	FString Registration;
	bool bPreviewValid = false;
};

void RegisterMenus()
{
	FToolMenuOwnerScoped Owner(TEXT("ProjectJContentAuthoring"));
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("ProjectJ"), LOCTEXT("Section", "Project J"));
	Section.AddMenuEntry(TEXT("ProjectJCreateContentBundle"), LOCTEXT("Menu", "Create Class / Advancement Bundle"),
		LOCTEXT("Tooltip", "Preview and create a connected, unsaved class or advancement template."), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			TSharedRef<SWindow> Window = SNew(SWindow).Title(LOCTEXT("Title", "Project J - Content Bundle"))
				.ClientSize(FVector2D(720, 680)).SupportsMinimize(false).SupportsMaximize(false)
				[SNew(SContentBundleWindow)];
			FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
		})));
}
}

#undef LOCTEXT_NAMESPACE
