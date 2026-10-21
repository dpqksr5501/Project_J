#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
#include "Authoring/Project_JContentBundleAuthoring.h"
#include "ToolMenus.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyNodeFactory.h"
#include "AnimPreviewInstance.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimationEditorViewportClient.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "EdMode.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "EditorModeManager.h"
#include "Framework/Application/SlateApplication.h"
#include "PersonaModule.h"
#include "Rendering/DrawElements.h"
#include "SAnimNotifyNode.h"
#include "SEditorViewport.h"
#include "ScopedTransaction.h"
#include "WeaponMotionEditing.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectIterator.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/Internationalization.h"
#include "Misc/EngineVersion.h"
class FProjectJWeaponMotionCancelContractTest;
#endif

#define LOCTEXT_NAMESPACE "ProjectJWeaponMotionEditor"

namespace Project_J::WeaponMotionEditor
{
	/** This mode replaces Persona's socket/bone editing only while a motion key is selected. */
	const FEditorModeID WeaponMotionEditModeId = TEXT("EM_Project_JWeaponMotion");

	static USceneComponent* FindPreviewWeaponComponent(USkeletalMeshComponent* MeshComponent)
	{
		if (!MeshComponent)
		{
			return nullptr;
		}

		TArray<USceneComponent*> Children;
		MeshComponent->GetChildrenComponents(true, Children);
		UStaticMeshComponent* FirstStaticMesh = nullptr;
		for (USceneComponent* Component : Children)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
			if (!StaticMeshComponent)
			{
				continue;
			}

			FirstStaticMesh = FirstStaticMesh ? FirstStaticMesh : StaticMeshComponent;
			if (StaticMeshComponent->DoesSocketExist(TEXT("WeaponGrip_R")) ||
				StaticMeshComponent->DoesSocketExist(TEXT("WeaponGroundProbe_Tip")) ||
				StaticMeshComponent->ComponentHasTag(TEXT("WeaponPresentationPreview")))
			{
				return StaticMeshComponent;
			}
		}

		return FirstStaticMesh;
	}

	static bool IsMotionNotifyActiveOnPreviewMesh(const UDebugSkelMeshComponent* PreviewMesh, const UProject_JAnimNotifyState_WeaponMotion* MotionNotify)
	{
		if (!PreviewMesh || !MotionNotify || !PreviewMesh->PreviewInstance)
		{
			return false;
		}

		const UAnimMontage* Montage = Cast<UAnimMontage>(PreviewMesh->PreviewInstance->GetAnimationAsset());
		if (!Montage)
		{
			return false;
		}

		const float MontageTime = PreviewMesh->PreviewInstance->GetCurrentTime();
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.NotifyStateClass == MotionNotify && MontageTime >= Event.GetTriggerTime() && MontageTime <= Event.GetEndTriggerTime())
			{
				return true;
			}
		}

		return false;
	}

	struct FPreviewBinding
	{
		TWeakObjectPtr<USceneComponent> WeaponComponent;
		TWeakObjectPtr<UProject_JAnimNotifyState_WeaponMotion> MotionNotify;
		FTransform BaseRelativeTransform = FTransform::Identity;
	};

	/** Editor-only state. The Montage stores only MotionKeys, never this selection. */
	struct FSelectedMotionKey
	{
		TWeakObjectPtr<UProject_JAnimNotifyState_WeaponMotion> MotionNotify;
		TWeakObjectPtr<UDebugSkelMeshComponent> PreviewMesh;
		TWeakObjectPtr<USceneComponent> WeaponComponent;
		int32 KeyIndex = INDEX_NONE;

		bool IsValid() const
		{
			return MotionNotify.IsValid() && MotionNotify->MotionKeys.IsValidIndex(KeyIndex) &&
				PreviewMesh.IsValid() && WeaponComponent.IsValid() &&
				IsMotionNotifyActiveOnPreviewMesh(PreviewMesh.Get(), MotionNotify.Get());
		}

		void Clear()
		{
			MotionNotify.Reset();
			PreviewMesh.Reset();
			WeaponComponent.Reset();
			KeyIndex = INDEX_NONE;
		}
	};

	static FSelectedMotionKey SelectedMotionKey;

	static FEditorViewportClient* FindPersonaViewport(const UDebugSkelMeshComponent* PreviewMesh)
	{
		if (!GEditor || !PreviewMesh)
		{
			return nullptr;
		}

		for (FEditorViewportClient* Candidate : GEditor->GetAllViewportClients())
		{
			if (!Candidate || !Candidate->GetModeTools())
			{
				continue;
			}

			// Only a Persona viewport has either Persona's skeleton mode or our mode.
			const FEditorModeTools* ModeTools = Candidate->GetModeTools();
			if (!ModeTools->IsModeActive(FPersonaEditModes::SkeletonSelection) && !ModeTools->IsModeActive(WeaponMotionEditModeId))
			{
				continue;
			}

			FAnimationViewportClient* PersonaViewport = static_cast<FAnimationViewportClient*>(Candidate);
			if (PersonaViewport->GetPreviewScene()->GetPreviewMeshComponent() == PreviewMesh)
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	static void SelectMotionKey(UProject_JAnimNotifyState_WeaponMotion* MotionNotify, int32 KeyIndex, const TSharedRef<SWidget>& SelectionWidget)
	{
		if (!MotionNotify || !MotionNotify->MotionKeys.IsValidIndex(KeyIndex) || !FSlateApplication::IsInitialized())
		{
			return;
		}
		const TSharedPtr<SWindow> SelectionWindow = FSlateApplication::Get().FindWidgetWindow(SelectionWidget);
		if (!SelectionWindow.IsValid()) { return; }

		UDebugSkelMeshComponent* ChosenMesh = nullptr;
		int32 CandidateCount = 0;
		for (TObjectIterator<UDebugSkelMeshComponent> It; It; ++It)
		{
			auto* Mesh = *It;
			if (Mesh->IsTemplate() || !IsMotionNotifyActiveOnPreviewMesh(Mesh, MotionNotify) || !FindPreviewWeaponComponent(Mesh)) { continue; }
			auto* Viewport = FindPersonaViewport(Mesh);
			if (!Viewport) { continue; }
			const TSharedPtr<SEditorViewport> ViewportWidget = Viewport->GetEditorViewportWidget();
			if (!ViewportWidget.IsValid() || FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef()) != SelectionWindow) { continue; }
			++CandidateCount; ChosenMesh = Mesh;
		}
		// A previously focused viewport may belong to another editor. Select only
		// the unique preview in the window that received this marker click.
		if (CandidateCount != 1) { return; }
		if (auto* Previous = FindPersonaViewport(SelectedMotionKey.PreviewMesh.Get()))
		{
			Previous->GetModeTools()->DeactivateMode(WeaponMotionEditModeId);
		}
		SelectedMotionKey.Clear();
		SelectedMotionKey.MotionNotify = MotionNotify;
		SelectedMotionKey.KeyIndex = KeyIndex;
		SelectedMotionKey.PreviewMesh = ChosenMesh;
		SelectedMotionKey.WeaponComponent = FindPreviewWeaponComponent(ChosenMesh);
		if (auto* Montage = Cast<UAnimMontage>(ChosenMesh->PreviewInstance->GetAnimationAsset()))
		{
			for (const auto& Event : Montage->Notifies)
			{
				if (Event.NotifyStateClass == MotionNotify)
				{
					ChosenMesh->PreviewInstance->MontagePreview_JumpToPosition(Event.GetTriggerTime() + Event.GetDuration() * MotionNotify->MotionKeys[KeyIndex].NormalizedTime);
					break;
				}
			}
		}

		if (FEditorViewportClient* ViewportClient = FindPersonaViewport(SelectedMotionKey.PreviewMesh.Get()))
		{
			FEditorModeTools* ModeTools = ViewportClient->GetModeTools();
			ModeTools->ActivateMode(WeaponMotionEditModeId);
			if (ModeTools->GetWidgetMode() == UE::Widget::WM_None || ModeTools->GetWidgetMode() == UE::Widget::WM_Scale)
			{
				ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
			}
			ViewportClient->Invalidate();
		}
	}

	/** Draws authored Transform-key times and turns a marker click into a viewport edit selection. */
	class SWeaponMotionNotifyNode final : public SAnimNotifyNode
	{
	public:
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const int32 ResultLayer = SAnimNotifyNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
			const FAnimNotifyEvent* Event = NodeObjectInterface ? NodeObjectInterface->GetNotifyEvent() : nullptr;
			const UProject_JAnimNotifyState_WeaponMotion* MotionNotify = Event ? Cast<UProject_JAnimNotifyState_WeaponMotion>(Event->NotifyStateClass) : nullptr;
			if (!MotionNotify || GetDurationSize() <= 0.0f)
			{
				return ResultLayer;
			}

			const FSlateBrush* MarkerBrush = FAppStyle::GetBrush(TEXT("WhiteBrush"));
			const float MarkerHeight = FMath::Min(14.0f, AllottedGeometry.GetLocalSize().Y);
			const float MarkerY = (AllottedGeometry.GetLocalSize().Y - MarkerHeight) * 0.5f;
			for (const FProject_JWeaponMotionKey& Key : MotionNotify->MotionKeys)
			{
				const float MarkerX = NotifyScrubHandleCentre + GetDurationSize() * FMath::Clamp(Key.NormalizedTime, 0.0f, 1.0f) - 1.0f;
				FSlateDrawElement::MakeBox(OutDrawElements, ResultLayer + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(2.0f, MarkerHeight), FSlateLayoutTransform(FVector2D(MarkerX, MarkerY))),
					MarkerBrush, ESlateDrawEffect::None, FLinearColor(1.0f, 0.95f, 0.25f, 1.0f));
			}
			return ResultLayer + 1;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && GetDurationSize() > 0.0f)
			{
				const FAnimNotifyEvent* Event = NodeObjectInterface ? NodeObjectInterface->GetNotifyEvent() : nullptr;
				UProject_JAnimNotifyState_WeaponMotion* MotionNotify = Event ? Cast<UProject_JAnimNotifyState_WeaponMotion>(Event->NotifyStateClass) : nullptr;
				if (MotionNotify)
				{
					const float LocalX = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
					int32 ClosestKeyIndex = INDEX_NONE;
					float ClosestDistance = TNumericLimits<float>::Max();
					for (int32 KeyIndex = 0; KeyIndex < MotionNotify->MotionKeys.Num(); ++KeyIndex)
					{
						const float KeyX = NotifyScrubHandleCentre + GetDurationSize() * FMath::Clamp(MotionNotify->MotionKeys[KeyIndex].NormalizedTime, 0.0f, 1.0f);
						const float Distance = FMath::Abs(LocalX - KeyX);
						if (Distance < ClosestDistance)
						{
							ClosestDistance = Distance;
							ClosestKeyIndex = KeyIndex;
						}
					}
					if (ClosestKeyIndex != INDEX_NONE && ClosestDistance <= 7.0f)
					{
						SelectMotionKey(MotionNotify, ClosestKeyIndex, SharedThis(this));
						return FReply::Handled();
					}
				}
			}
			return SAnimNotifyNode::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
	};

	/** Standard Persona translate/rotate widget; changes are written to exactly one selected inline MotionKey. */
	class FWeaponMotionEditMode final : public FEdMode
	{
	public:
		virtual bool UsesTransformWidget() const override { return OwnsSelection(); }
		virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override
		{
			return OwnsSelection() && (CheckMode == UE::Widget::WM_Translate || CheckMode == UE::Widget::WM_Rotate || CheckMode == UE::Widget::WM_TranslateRotateZ);
		}
		virtual bool ShouldDrawWidget() const override { return OwnsSelection(); }
		virtual FVector GetWidgetLocation() const override
		{
			return OwnsSelection() ? SelectedMotionKey.WeaponComponent->GetComponentLocation() : FVector::ZeroVector;
		}
		virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override
		{
			if (!OwnsSelection()) return false;
			OutMatrix = SelectedMotionKey.WeaponComponent->GetComponentTransform().ToMatrixNoScale().RemoveTranslation();
			return true;
		}
		virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override
		{
			return GetCustomDrawingCoordinateSystem(OutMatrix, InData);
		}

		virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override
		{
			if (!OwnsSelection() || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None) return false;
			return Transaction || BeginKeyEdit();
		}

		virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override
		{
			if (!Transaction) return false;
			FinishTransaction(false);
			return true;
		}

		virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
		{
			if (!Transaction || !OwnsSelection() || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None) return false;
			const UE::Widget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
			const bool bTranslate = WidgetMode == UE::Widget::WM_Translate || WidgetMode == UE::Widget::WM_TranslateRotateZ;
			const bool bRotate = WidgetMode == UE::Widget::WM_Rotate || WidgetMode == UE::Widget::WM_TranslateRotateZ;
			if (!bTranslate && !bRotate) return false;

			USceneComponent* WeaponComponent = SelectedMotionKey.WeaponComponent.Get();
			UProject_JAnimNotifyState_WeaponMotion* MotionNotify = SelectedMotionKey.MotionNotify.Get();
			if (!WeaponComponent || !MotionNotify || !MotionNotify->MotionKeys.IsValidIndex(SelectedMotionKey.KeyIndex)) return false;

			auto* Parent = WeaponComponent->GetAttachParent();
			const FTransform SocketWorld = Parent ? Parent->GetSocketTransform(WeaponComponent->GetAttachSocketName(), RTS_World) : FTransform::Identity;
			if (!ApplyKeyDelta(SocketWorld, bTranslate ? InDrag : FVector::ZeroVector, bRotate ? InRot : FRotator::ZeroRotator)) { return false; }

			MotionNotify->MarkPackageDirty();
			InViewport->Invalidate();
			return true;
		}

		virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey Key, EInputEvent Event) override
		{
			if (Key == EKeys::Escape && Event == IE_Pressed)
			{
				FinishTransaction(true);
				SelectedMotionKey.Clear();
				if (auto* Manager = GetModeManager()) Manager->DeactivateMode(WeaponMotionEditModeId);
				return true;
			}
			return FEdMode::InputKey(InViewportClient, InViewport, Key, Event);
		}

		virtual void Exit() override { FinishTransaction(false); FEdMode::Exit(); }
		virtual void Tick(FEditorViewportClient* Client, float DeltaTime) override
		{
			FEdMode::Tick(Client, DeltaTime);
			if (Transaction && !OwnsSelection()) { FinishTransaction(false); }
		}
	private:
#if WITH_DEV_AUTOMATION_TESTS
		friend class ::FProjectJWeaponMotionCancelContractTest;
#endif
		bool OwnsSelection() const
		{
			auto* Viewport = FindPersonaViewport(SelectedMotionKey.PreviewMesh.Get());
			return SelectedMotionKey.IsValid() && Viewport && Viewport->GetModeTools() == GetModeManager();
		}
		bool BeginKeyEdit()
		{
			auto* Notify = SelectedMotionKey.MotionNotify.Get();
			if (!GEditor || GEditor->IsTransactionActive() || !Notify || !Notify->MotionKeys.IsValidIndex(SelectedMotionKey.KeyIndex)) return false;
			Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditWeaponMotionKey", "Edit Weapon Motion Key"));
			if (!Transaction->IsOutstanding()) { Transaction.Reset(); return false; }
			Notify->SetFlags(RF_Transactional); Notify->Modify();
			EditedNotify = Notify; EditedIndex = SelectedMotionKey.KeyIndex; EditedKeyCount = Notify->MotionKeys.Num();
			EditedTime = Notify->MotionKeys[EditedIndex].NormalizedTime;
			OriginalTransform = LastWrittenTransform = Notify->MotionKeys[EditedIndex].RelativeTransform;
			return true;
		}
		FProject_JWeaponMotionKey* OwnedKey() const
		{
			auto* Notify = EditedNotify.Get();
			if (!Transaction || !Notify || Notify->MotionKeys.Num() != EditedKeyCount || !Notify->MotionKeys.IsValidIndex(EditedIndex)) return nullptr;
			auto& Key = Notify->MotionKeys[EditedIndex];
			return Key.NormalizedTime == EditedTime && Key.RelativeTransform.Equals(LastWrittenTransform, 0.0) ? &Key : nullptr;
		}
		bool ApplyKeyDelta(const FTransform& SocketWorld, const FVector& Drag, const FRotator& Rotation)
		{
			auto* Key = OwnedKey();
			if (!Key || SelectedMotionKey.MotionNotify != EditedNotify || SelectedMotionKey.KeyIndex != EditedIndex ||
				!ApplyWorldDelta(Key->RelativeTransform, SocketWorld, Drag, Rotation)) return false;
			LastWrittenTransform = Key->RelativeTransform;
			return true;
		}
		void FinishTransaction(bool bCancel)
		{
			if (!Transaction) return;
			bool bRestoredOwnedKey = false;
			if (bCancel)
			{
				// Cancel discards the undo record; it does not restore object values.
				// Restore only this gesture's unchanged key, never an external edit.
				if (auto* Key = OwnedKey()) { Key->RelativeTransform = OriginalTransform; bRestoredOwnedKey = true; }
			}
			auto Finished = MoveTemp(Transaction);
			EditedNotify.Reset(); EditedIndex = INDEX_NONE;
			// A foreign edit in the same transaction keeps its undo record.
			if (bRestoredOwnedKey) Finished->Cancel();
		}
		TUniquePtr<FScopedTransaction> Transaction;
		TWeakObjectPtr<UProject_JAnimNotifyState_WeaponMotion> EditedNotify;
		int32 EditedIndex = INDEX_NONE;
		int32 EditedKeyCount = 0;
		float EditedTime = 0;
		FTransform OriginalTransform = FTransform::Identity;
		FTransform LastWrittenTransform = FTransform::Identity;
	};
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponMotionCancelContractTest, "ProjectJ.PCStructure.WeaponMotionCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponMotionCancelContractTest::RunTest(const FString&)
{
	using namespace Project_J::WeaponMotionEditor;
	if (!TestNotNull(TEXT("Editor transaction authority exists"), GEditor) || !TestFalse(TEXT("No foreign transaction is active"), GEditor->IsTransactionActive())) return false;
	const auto PreviousSelection = SelectedMotionKey;
	ON_SCOPE_EXIT { SelectedMotionKey = PreviousSelection; };
	auto* Notify = NewObject<UProject_JAnimNotifyState_WeaponMotion>();
	Notify->MotionKeys.SetNum(1); Notify->MotionKeys[0].NormalizedTime = .25f;
	const FTransform Original(FQuat::Identity, FVector(1, 2, 3));
	SelectedMotionKey.MotionNotify = Notify; SelectedMotionKey.KeyIndex = 0;
	{
		TGuardValue<TObjectPtr<UTransactor>> NoTransactor(GEditor->Trans, nullptr);
		FWeaponMotionEditMode Edit;
		TestFalse(TEXT("Editing is rejected when no transaction can be recorded"), Edit.BeginKeyEdit());
		TestFalse(TEXT("Rejected editing cannot change the key"), Edit.ApplyKeyDelta(FTransform::Identity, FVector(10, 0, 0), FRotator::ZeroRotator));
	}
	for (int32 Mode : {0, 1, 2, 3})
	{
		Notify->MotionKeys.SetNum(1); Notify->MotionKeys[0].RelativeTransform = Original;
		FWeaponMotionEditMode Edit;
		ON_SCOPE_EXIT { Edit.FinishTransaction(false); };
		const int32 QueueBefore = GEditor->Trans->GetQueueLength();
		if (!TestTrue(TEXT("Actual edit mode begins its own transaction"), Edit.BeginKeyEdit())) return false;
		if (!TestTrue(TEXT("Actual delta writer changes its captured key"), Edit.ApplyKeyDelta(FTransform::Identity, FVector(10, 0, 0), FRotator::ZeroRotator))) return false;
		const FTransform Written = Notify->MotionKeys[0].RelativeTransform;
		if (Mode == 1) Edit.EndTracking(nullptr, nullptr);
		if (Mode == 2) Notify->MotionKeys[0].RelativeTransform.SetTranslation(FVector(99));
		if (Mode == 3) Notify->MotionKeys.AddDefaulted();
		Edit.InputKey(nullptr, nullptr, EKeys::Escape, IE_Pressed);
		if (Mode == 0) TestTrue(TEXT("Escape restores the active gesture value"), Notify->MotionKeys[0].RelativeTransform.Equals(Original));
		if (Mode == 1) TestTrue(TEXT("Escape after completed drag preserves the committed value"), Notify->MotionKeys[0].RelativeTransform.Equals(Written));
		if (Mode == 2) TestTrue(TEXT("Cancel preserves a newer external key edit"), Notify->MotionKeys[0].RelativeTransform.GetTranslation().Equals(FVector(99)));
		if (Mode == 3) TestTrue(TEXT("Cancel cannot overwrite a changed key collection"), Notify->MotionKeys.Num() == 2 && Notify->MotionKeys[0].RelativeTransform.Equals(Written));
		if (Mode == 2 || Mode == 3) TestEqual(TEXT("Foreign edits retain their transaction undo record"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);
		TestFalse(TEXT("The gesture leaves no active transaction"), GEditor->IsTransactionActive());
		SelectedMotionKey.MotionNotify = Notify; SelectedMotionKey.KeyIndex = 0;
	}
	return true;
}
#endif

class FProject_JCharacterEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS
		// UE 5.8.3's three Core formatting tests compare localized FText output
		// with English literals. Run their source-language contract in isolation;
		// never skip assertions, suppress errors, or change the user's settings.
		if (FEngineVersion::Current().GetMajor()==5 && FEngineVersion::Current().GetMinor()==8 && FEngineVersion::Current().GetPatch()==3)
		{
			auto& Tests=FAutomationTestFramework::Get();
			TestCultureStartHandle=Tests.OnTestStartEvent.AddRaw(this,&FProject_JCharacterEditorModule::BeginEngineFormatTest);
			TestCultureEndHandle=Tests.OnTestEndEvent.AddRaw(this,&FProject_JCharacterEditorModule::EndEngineFormatTest);
		}
#endif
		if (!IsRunningCommandlet())
			ContentMenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&ProjectJ::ContentAuthoring::RegisterMenus));
		FEditorModeRegistry::Get().RegisterMode<Project_J::WeaponMotionEditor::FWeaponMotionEditMode>(Project_J::WeaponMotionEditor::WeaponMotionEditModeId, LOCTEXT("WeaponMotionEditMode", "Weapon Motion Key"), FSlateIcon(), false);
		NotifyNodeFactoryHandle = FAnimNotifyNodeFactory::RegisterFactory([](FAnimNotifyEvent* Event) -> TSharedPtr<SAnimNotifyNode>
		{
			if (Event && Cast<UProject_JAnimNotifyState_WeaponMotion>(Event->NotifyStateClass))
			{
				return SNew(Project_J::WeaponMotionEditor::SWeaponMotionNotifyNode);
			}
			return nullptr;
		});
		PreviewTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FProject_JCharacterEditorModule::TickMontageWeaponPreview));
	}

	virtual void ShutdownModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS
		FAutomationTestFramework::Get().OnTestStartEvent.Remove(TestCultureStartHandle);
		FAutomationTestFramework::Get().OnTestEndEvent.Remove(TestCultureEndHandle);
		RestoreTestCulture();
#endif
		UToolMenus::UnRegisterStartupCallback(ContentMenuHandle);
		UToolMenus::UnregisterOwner(TEXT("ProjectJContentAuthoring"));
		if (NotifyNodeFactoryHandle.IsValid()) FAnimNotifyNodeFactory::UnregisterFactory(NotifyNodeFactoryHandle);
		if (PreviewTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(PreviewTickerHandle);
		for (TPair<TWeakObjectPtr<UDebugSkelMeshComponent>, Project_J::WeaponMotionEditor::FPreviewBinding>& Pair : PreviewBindings)
		{
			if (USceneComponent* WeaponComponent = Pair.Value.WeaponComponent.Get()) WeaponComponent->SetRelativeTransform(Pair.Value.BaseRelativeTransform);
		}
		PreviewBindings.Empty();
		if (auto* Viewport = Project_J::WeaponMotionEditor::FindPersonaViewport(Project_J::WeaponMotionEditor::SelectedMotionKey.PreviewMesh.Get()))
		{ Viewport->GetModeTools()->DeactivateMode(Project_J::WeaponMotionEditor::WeaponMotionEditModeId); }
		Project_J::WeaponMotionEditor::SelectedMotionKey.Clear();
		FEditorModeRegistry::Get().UnregisterMode(Project_J::WeaponMotionEditor::WeaponMotionEditModeId);
	}

private:
#if WITH_DEV_AUTOMATION_TESTS
	void BeginEngineFormatTest(FAutomationTestBase* Test)
	{
		const FString Name=Test->GetTestName();
		if (Name!=TEXT("FUnifiedErrorTest_CreateErrorMessage") && Name!=TEXT("FUnifiedErrorTest_CreateErrorMessageWithContext") && Name!=TEXT("FStructuredLogFormatTest")) return;
		check(IsInGameThread() && !TestCulture.IsSet());
		auto& I18N=FInternationalization::Get();
		TestCulture.Emplace();I18N.BackupCultureState(TestCulture.GetValue());
		ActiveCultureTest=Test;
		if (!I18N.SetCurrentLanguageAndLocale(TEXT("en"))) Test->AddError(TEXT("Cannot establish the engine formatting test's English source-language contract"));
	}
	void EndEngineFormatTest(FAutomationTestBase* Test)
	{
		if (Test==ActiveCultureTest) RestoreTestCulture();
	}
	void RestoreTestCulture()
	{
		if (TestCulture.IsSet() && FInternationalization::IsAvailable()) FInternationalization::Get().RestoreCultureState(TestCulture.GetValue());
		TestCulture.Reset();ActiveCultureTest=nullptr;
	}
	TOptional<FInternationalization::FCultureStateSnapshot> TestCulture;
	FAutomationTestBase* ActiveCultureTest=nullptr;
	FDelegateHandle TestCultureStartHandle,TestCultureEndHandle;
#endif
	bool TickMontageWeaponPreview(float DeltaTime)
	{
		TSet<TWeakObjectPtr<UDebugSkelMeshComponent>> ActivePreviewMeshes;
		for (TObjectIterator<UDebugSkelMeshComponent> It; It; ++It)
		{
			UDebugSkelMeshComponent* PreviewMesh = *It;
			if (!PreviewMesh || PreviewMesh->IsTemplate() || !PreviewMesh->PreviewInstance) continue;
			UAnimMontage* Montage = Cast<UAnimMontage>(PreviewMesh->PreviewInstance->GetAnimationAsset());
			if (!Montage) continue;

			const float MontageTime = PreviewMesh->PreviewInstance->GetCurrentTime();
			const FAnimNotifyEvent* ActiveEvent = nullptr;
			UProject_JAnimNotifyState_WeaponMotion* ActiveMotionNotify = nullptr;
			for (const FAnimNotifyEvent& Event : Montage->Notifies)
			{
				UProject_JAnimNotifyState_WeaponMotion* MotionNotify = Cast<UProject_JAnimNotifyState_WeaponMotion>(Event.NotifyStateClass);
				if (MotionNotify && MontageTime >= Event.GetTriggerTime() && MontageTime <= Event.GetEndTriggerTime())
				{
					ActiveEvent = &Event;
					ActiveMotionNotify = MotionNotify;
					break;
				}
			}
			if (!ActiveEvent || !ActiveMotionNotify) continue;
			if (!FMath::IsFinite(MontageTime) || !Project_J::WeaponMotion::GetValidationFailure(ActiveMotionNotify->MotionKeys,
				ActiveMotionNotify->PrimaryGripIKAlpha, ActiveMotionNotify->SecondaryGripIKAlpha, ActiveEvent->GetDuration(),
				ActiveMotionNotify->EntryBlendSeconds, ActiveMotionNotify->ExitBlendSeconds).IsNone()) continue;

			ActivePreviewMeshes.Add(PreviewMesh);
			Project_J::WeaponMotionEditor::FPreviewBinding& Binding = PreviewBindings.FindOrAdd(PreviewMesh);
			if (Binding.MotionNotify.Get() != ActiveMotionNotify || !Binding.WeaponComponent.IsValid())
			{
				if (USceneComponent* OldWeaponComponent = Binding.WeaponComponent.Get()) OldWeaponComponent->SetRelativeTransform(Binding.BaseRelativeTransform);
				USceneComponent* WeaponComponent = Project_J::WeaponMotionEditor::FindPreviewWeaponComponent(PreviewMesh);
				if (!WeaponComponent)
				{
					PreviewBindings.Remove(PreviewMesh);
					continue;
				}
				Binding.WeaponComponent = WeaponComponent;
				Binding.MotionNotify = ActiveMotionNotify;
				// Never sample a frame here: it can contain an old key after reload or a scrub jump.
				Binding.BaseRelativeTransform = FTransform::Identity;
				WeaponComponent->SetRelativeTransform(Binding.BaseRelativeTransform);
			}

			if (USceneComponent* WeaponComponent = Binding.WeaponComponent.Get())
			{
				const float Duration = FMath::Max(ActiveEvent->GetDuration(), UE_KINDA_SMALL_NUMBER);
				const float NormalizedTime = FMath::Clamp((MontageTime - ActiveEvent->GetTriggerTime()) / Duration, 0.0f, 1.0f);
				const FTransform PreviewTransform = Binding.BaseRelativeTransform * Project_J::WeaponMotion::EvaluateStateTransform(ActiveMotionNotify->MotionKeys, NormalizedTime, Duration, ActiveMotionNotify->EntryBlendSeconds, ActiveMotionNotify->ExitBlendSeconds);
				if (!PreviewTransform.ContainsNaN()) WeaponComponent->SetRelativeTransform(PreviewTransform);
				if (Project_J::WeaponMotionEditor::SelectedMotionKey.MotionNotify.Get() == ActiveMotionNotify &&
					Project_J::WeaponMotionEditor::SelectedMotionKey.PreviewMesh.Get() == PreviewMesh)
				{
					Project_J::WeaponMotionEditor::SelectedMotionKey.PreviewMesh = PreviewMesh;
					Project_J::WeaponMotionEditor::SelectedMotionKey.WeaponComponent = WeaponComponent;
				}
			}
		}

		for (auto It = PreviewBindings.CreateIterator(); It; ++It)
		{
			if (!ActivePreviewMeshes.Contains(It.Key()) || !It.Key().IsValid())
			{
				if (USceneComponent* WeaponComponent = It.Value().WeaponComponent.Get()) WeaponComponent->SetRelativeTransform(It.Value().BaseRelativeTransform);
				It.RemoveCurrent();
			}
		}
		return true;
	}

	FDelegateHandle NotifyNodeFactoryHandle;
	FDelegateHandle ContentMenuHandle;
	FTSTicker::FDelegateHandle PreviewTickerHandle;
	TMap<TWeakObjectPtr<UDebugSkelMeshComponent>, Project_J::WeaponMotionEditor::FPreviewBinding> PreviewBindings;
};

IMPLEMENT_MODULE(FProject_JCharacterEditorModule, Project_JCharacterEditor);

#undef LOCTEXT_NAMESPACE
