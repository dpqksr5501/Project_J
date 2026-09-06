#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
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
#include "PersonaModule.h"
#include "Rendering/DrawElements.h"
#include "SAnimNotifyNode.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectIterator.h"

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

	static void SelectMotionKey(UProject_JAnimNotifyState_WeaponMotion* MotionNotify, int32 KeyIndex)
	{
		if (!MotionNotify || !MotionNotify->MotionKeys.IsValidIndex(KeyIndex))
		{
			return;
		}

		SelectedMotionKey.Clear();
		SelectedMotionKey.MotionNotify = MotionNotify;
		SelectedMotionKey.KeyIndex = KeyIndex;

		for (TObjectIterator<UDebugSkelMeshComponent> It; It; ++It)
		{
			UDebugSkelMeshComponent* PreviewMesh = *It;
			if (!PreviewMesh || PreviewMesh->IsTemplate() || !IsMotionNotifyActiveOnPreviewMesh(PreviewMesh, MotionNotify))
			{
				continue;
			}

			if (USceneComponent* WeaponComponent = FindPreviewWeaponComponent(PreviewMesh))
			{
				SelectedMotionKey.PreviewMesh = PreviewMesh;
				SelectedMotionKey.WeaponComponent = WeaponComponent;
				if (UAnimMontage* Montage = Cast<UAnimMontage>(PreviewMesh->PreviewInstance->GetAnimationAsset()))
				{
					for (const FAnimNotifyEvent& Event : Montage->Notifies)
					{
						if (Event.NotifyStateClass == MotionNotify)
						{
							const float KeyTime = Event.GetTriggerTime() + Event.GetDuration() * MotionNotify->MotionKeys[KeyIndex].NormalizedTime;
							PreviewMesh->PreviewInstance->MontagePreview_JumpToPosition(KeyTime);
							break;
						}
					}
				}
				break;
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
						SelectMotionKey(MotionNotify, ClosestKeyIndex);
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
		virtual bool UsesTransformWidget() const override { return SelectedMotionKey.IsValid(); }
		virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override
		{
			return SelectedMotionKey.IsValid() && (CheckMode == UE::Widget::WM_Translate || CheckMode == UE::Widget::WM_Rotate || CheckMode == UE::Widget::WM_TranslateRotateZ);
		}
		virtual bool ShouldDrawWidget() const override { return SelectedMotionKey.IsValid(); }
		virtual FVector GetWidgetLocation() const override
		{
			return SelectedMotionKey.IsValid() ? SelectedMotionKey.WeaponComponent->GetComponentLocation() : FVector::ZeroVector;
		}
		virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override
		{
			if (!SelectedMotionKey.IsValid()) return false;
			OutMatrix = SelectedMotionKey.WeaponComponent->GetComponentTransform().ToMatrixNoScale().RemoveTranslation();
			return true;
		}
		virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override
		{
			return GetCustomDrawingCoordinateSystem(OutMatrix, InData);
		}

		virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override
		{
			if (!SelectedMotionKey.IsValid() || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None) return false;
			if (!bInTransaction)
			{
				GEditor->BeginTransaction(LOCTEXT("EditWeaponMotionKey", "Edit Weapon Motion Key"));
				SelectedMotionKey.MotionNotify->SetFlags(RF_Transactional);
				SelectedMotionKey.MotionNotify->Modify();
				bInTransaction = true;
			}
			bManipulating = true;
			return true;
		}

		virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override
		{
			if (!bManipulating) return false;
			if (bInTransaction)
			{
				GEditor->EndTransaction();
				bInTransaction = false;
			}
			bManipulating = false;
			return true;
		}

		virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
		{
			if (!bManipulating || !SelectedMotionKey.IsValid() || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None) return false;
			const UE::Widget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();
			const bool bTranslate = WidgetMode == UE::Widget::WM_Translate || WidgetMode == UE::Widget::WM_TranslateRotateZ;
			const bool bRotate = WidgetMode == UE::Widget::WM_Rotate || WidgetMode == UE::Widget::WM_TranslateRotateZ;
			if (!bTranslate && !bRotate) return false;

			USceneComponent* WeaponComponent = SelectedMotionKey.WeaponComponent.Get();
			UProject_JAnimNotifyState_WeaponMotion* MotionNotify = SelectedMotionKey.MotionNotify.Get();
			if (!WeaponComponent || !MotionNotify || !MotionNotify->MotionKeys.IsValidIndex(SelectedMotionKey.KeyIndex)) return false;

			FProject_JWeaponMotionKey& Key = MotionNotify->MotionKeys[SelectedMotionKey.KeyIndex];
			const FTransform ParentWorldTransform = WeaponComponent->GetAttachParent() ? WeaponComponent->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
			if (bTranslate && !InDrag.IsNearlyZero())
			{
				// Widget deltas are world-space; stored transforms are relative to the attachment socket.
				Key.RelativeTransform.AddToTranslation(ParentWorldTransform.InverseTransformVectorNoScale(InDrag));
			}
			if (bRotate && !InRot.IsNearlyZero())
			{
				FVector WorldAxis;
				float AngleRadians = 0.0f;
				InRot.Quaternion().ToAxisAndAngle(WorldAxis, AngleRadians);
				if (!WorldAxis.IsNearlyZero() && !FMath::IsNearlyZero(AngleRadians))
				{
					const FVector LocalAxis = ParentWorldTransform.InverseTransformVectorNoScale(WorldAxis).GetSafeNormal();
					Key.RelativeTransform.ConcatenateRotation(FQuat(LocalAxis, AngleRadians));
					Key.RelativeTransform.NormalizeRotation();
				}
			}
			MotionNotify->MarkPackageDirty();
			InViewport->Invalidate();
			return true;
		}

		virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey Key, EInputEvent Event) override
		{
			if (Key == EKeys::Escape && Event == IE_Pressed)
			{
				SelectedMotionKey.Clear();
				GetModeManager()->DeactivateMode(WeaponMotionEditModeId);
				return true;
			}
			return FEdMode::InputKey(InViewportClient, InViewport, Key, Event);
		}

	private:
		bool bManipulating = false;
		bool bInTransaction = false;
	};
}

class FProject_JCharacterEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
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
		if (NotifyNodeFactoryHandle.IsValid()) FAnimNotifyNodeFactory::UnregisterFactory(NotifyNodeFactoryHandle);
		if (PreviewTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(PreviewTickerHandle);
		for (TPair<TWeakObjectPtr<UDebugSkelMeshComponent>, Project_J::WeaponMotionEditor::FPreviewBinding>& Pair : PreviewBindings)
		{
			if (USceneComponent* WeaponComponent = Pair.Value.WeaponComponent.Get()) WeaponComponent->SetRelativeTransform(Pair.Value.BaseRelativeTransform);
		}
		PreviewBindings.Empty();
		Project_J::WeaponMotionEditor::SelectedMotionKey.Clear();
		FEditorModeRegistry::Get().UnregisterMode(Project_J::WeaponMotionEditor::WeaponMotionEditModeId);
	}

private:
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
				WeaponComponent->SetRelativeTransform(Binding.BaseRelativeTransform * Project_J::WeaponMotion::EvaluateStateTransform(ActiveMotionNotify->MotionKeys, NormalizedTime, Duration, ActiveMotionNotify->EntryBlendSeconds, ActiveMotionNotify->ExitBlendSeconds));
				if (Project_J::WeaponMotionEditor::SelectedMotionKey.MotionNotify.Get() == ActiveMotionNotify)
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
	FTSTicker::FDelegateHandle PreviewTickerHandle;
	TMap<TWeakObjectPtr<UDebugSkelMeshComponent>, Project_J::WeaponMotionEditor::FPreviewBinding> PreviewBindings;
};

IMPLEMENT_MODULE(FProject_JCharacterEditorModule, Project_JCharacterEditor);

#undef LOCTEXT_NAMESPACE
