#include "Components/Project_JPlayerInputBindingComponent.h"

#include "EnhancedInputComponent.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "InputActionValue.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include <functional>
#include <type_traits>

void UProject_JPlayerInputBindingComponent::BeginPlay()
{
	bEnding = false;
	Super::BeginPlay();
}

UProject_JPlayerInputBindingComponent::UProject_JPlayerInputBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

bool UProject_JPlayerInputBindingComponent::BindInput(UInputComponent* PlayerInputComponent, AProject_JPlayerCharacter* PlayerCharacter, const FProject_JPlayerInputActionSet& ActionSet)
{
	if (bEnding || IsBeingDestroyed()) { return false; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TStrongObjectPtr<AProject_JPlayerCharacter> PinnedPlayer(PlayerCharacter);
	TStrongObjectPtr<UInputComponent> PinnedInput(PlayerInputComponent);
	const uint64 ExpectedRevision = BindingRevision + 1;
	UnbindInput();
	if (bEnding || IsBeingDestroyed() || BindingRevision != ExpectedRevision) { return false; }
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(EnhancedInputComponent) || EnhancedInputComponent->IsBeingDestroyed() ||
		!IsValid(PlayerCharacter) || PlayerCharacter->IsActorBeingDestroyed()) { return false; }
	BoundPlayerCharacter = PlayerCharacter;
	ActiveSkillInputMappingData = ActionSet.SkillInputMappingData;
	ActiveDirectInputTags.Reset();

	BoundEnhancedInputComponent = EnhancedInputComponent;
	if (UProject_JMountComponent* Mount = BoundPlayerCharacter->GetMountComponent())
	{
		Mount->SetRiderInteractAction(ActionSet.InteractAction);
	}
	const uint64 SourceRevision = BindingRevision;
	const TWeakObjectPtr<ThisClass> SourceBinding(this);
	const TWeakObjectPtr<AProject_JPlayerCharacter> SourcePlayer(PlayerCharacter);
	const TWeakObjectPtr<UEnhancedInputComponent> SourceDevice(EnhancedInputComponent);
	const auto BindOwnedAction = [this, EnhancedInputComponent, SourceRevision, SourceBinding, SourcePlayer, SourceDevice]
		(auto Action, ETriggerEvent Event, auto* Object, auto Method, auto... Args)
	{
		const UInputAction* InputAction = Action;
		if (!InputAction) { return; }
		using FObjectType = std::remove_pointer_t<decltype(Object)>;
		const TWeakObjectPtr<FObjectType> SourceObject(Object);
		const TWeakObjectPtr<const UInputAction> SourceAction(InputAction);
		// EnhancedPlayerInput clones a frame's delegates before executing them.
		// Removing a binding cannot revoke that copy; its original receipt must match.
		auto Callback = [SourceRevision, SourceBinding, SourcePlayer, SourceDevice, SourceObject, SourceAction, Method, Args...]
			(const FInputActionValue& Value)
		{
			auto* Self = SourceBinding.Get(); auto* Player = SourcePlayer.Get(); auto* Device = SourceDevice.Get();
			auto* Receiver = SourceObject.Get();
			if (!Self || !Self->IsCurrentBinding(Player, SourceRevision) || !IsValid(Device) || Device->IsBeingDestroyed()
				|| Self->BoundEnhancedInputComponent.Get() != Device || !IsValid(Receiver) || !SourceAction.IsValid()) { return; }
			const TStrongObjectPtr<ThisClass> PinnedSelf(Self);
			const TStrongObjectPtr<AProject_JPlayerCharacter> PinnedPlayer(Player);
			const TStrongObjectPtr<UEnhancedInputComponent> PinnedDevice(Device);
			const TStrongObjectPtr<FObjectType> PinnedReceiver(Receiver);
			const TStrongObjectPtr<const UInputAction> PinnedAction(SourceAction.Get());
			if constexpr (std::is_invocable_v<decltype(Method), FObjectType*, const FInputActionValue&, decltype(Args)...>)
			{ std::invoke(Method, Receiver, Value, Args...); }
			else { std::invoke(Method, Receiver, Args...); }
		};
		OwnedBindingHandles.Add(EnhancedInputComponent->BindActionValueLambda(InputAction, Event, MoveTemp(Callback)).GetHandle());
	};

	BindOwnedAction(ActionSet.JumpAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleJumpStarted);
	BindOwnedAction(ActionSet.JumpAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleJumpStopped);
	BindOwnedAction(ActionSet.JumpAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleJumpStopped);

	BindOwnedAction(ActionSet.MoveAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleMove);
	BindOwnedAction(ActionSet.MoveAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);
	BindOwnedAction(ActionSet.MoveAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);

	const bool bHasCompleteSemanticMoveIntentActionSet =
		ActionSet.MoveIntentForwardAction &&
		ActionSet.MoveIntentBackwardAction &&
		ActionSet.MoveIntentLeftAction &&
		ActionSet.MoveIntentRightAction;
	bSemanticMoveIntentActionsBound = bHasCompleteSemanticMoveIntentActionSet;
	bPendingSemanticMoveIntentRefresh = false;
	SemanticMoveIntent.Clear();

	const auto BindMoveIntentAction = [BindOwnedAction, this](UInputAction* Action, const EProject_JMoveIntentDirection Direction)
	{
		if (!Action)
		{
			return;
		}

		BindOwnedAction(Action, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStarted, Direction);
		BindOwnedAction(Action, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStopped, Direction);
		BindOwnedAction(Action, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStopped, Direction);
	};
	if (bSemanticMoveIntentActionsBound)
	{
		BindMoveIntentAction(ActionSet.MoveIntentForwardAction, EProject_JMoveIntentDirection::Forward);
		BindMoveIntentAction(ActionSet.MoveIntentBackwardAction, EProject_JMoveIntentDirection::Backward);
		BindMoveIntentAction(ActionSet.MoveIntentLeftAction, EProject_JMoveIntentDirection::Left);
		BindMoveIntentAction(ActionSet.MoveIntentRightAction, EProject_JMoveIntentDirection::Right);
	}

	BindOwnedAction(ActionSet.MouseLookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);
	BindOwnedAction(ActionSet.LookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);

	BindOwnedAction(ActionSet.SprintAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StartSprint);
	BindOwnedAction(ActionSet.SprintAction, ETriggerEvent::Completed, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StopSprint);
	BindOwnedAction(ActionSet.SprintAction, ETriggerEvent::Canceled, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StopSprint);
	BindOwnedAction(ActionSet.ToggleCombatAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::ToggleCombatMode);
	if (ActionSet.AttackAction)
	{
		BindOwnedAction(ActionSet.AttackAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillPressed);
		BindOwnedAction(ActionSet.AttackAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased);
		BindOwnedAction(ActionSet.AttackAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased);
	}
	if (ActionSet.HeavyAttackAction)
	{
		BindOwnedAction(ActionSet.HeavyAttackAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillPressed);
		BindOwnedAction(ActionSet.HeavyAttackAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased);
		BindOwnedAction(ActionSet.HeavyAttackAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased);
	}
	if (ActionSet.SkillModifierAction)
	{
		BindOwnedAction(ActionSet.SkillModifierAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierPressed);
		BindOwnedAction(ActionSet.SkillModifierAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierReleased);
		BindOwnedAction(ActionSet.SkillModifierAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierReleased);
	}

	TSet<const UInputAction*> BoundSkillActions;
	if (const UProject_JSkillInputMappingData* SkillInputMappingData = ActiveSkillInputMappingData)
	{
		if (UProject_JSkillInputRouterComponent* SkillInputRouter = BoundPlayerCharacter->SkillInputRouterComponent)
		{
			for (const FProject_JSkillModifierBinding& ModifierBinding : SkillInputMappingData->ModifierBindings)
			{
				if (!ModifierBinding.InputAction || !ModifierBinding.ModifierTag.IsValid() || BoundSkillActions.Contains(ModifierBinding.InputAction))
				{
					continue;
				}

				BoundSkillActions.Add(ModifierBinding.InputAction);
				BindOwnedAction(ModifierBinding.InputAction, ETriggerEvent::Started, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierPressed, ModifierBinding.ModifierTag);
				BindOwnedAction(ModifierBinding.InputAction, ETriggerEvent::Completed, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierReleased, ModifierBinding.ModifierTag);
				BindOwnedAction(ModifierBinding.InputAction, ETriggerEvent::Canceled, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierReleased, ModifierBinding.ModifierTag);
			}
		}

		for (const FProject_JDirectSkillInputBinding& SkillBinding : SkillInputMappingData->DirectSkillBindings)
		{
			if (!SkillBinding.InputAction || !SkillBinding.InputTag.IsValid() || BoundSkillActions.Contains(SkillBinding.InputAction))
			{
				continue;
			}

			BoundSkillActions.Add(SkillBinding.InputAction);
			BindOwnedAction(SkillBinding.InputAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionPressed, SkillBinding.InputAction.Get());
			BindOwnedAction(SkillBinding.InputAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased, SkillBinding.InputAction.Get());
			BindOwnedAction(SkillBinding.InputAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased, SkillBinding.InputAction.Get());
		}
	}

	if (ActionSet.InteractAction)
	{
		BindOwnedAction(ActionSet.InteractAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleInteract);
	}

	return true;
}

void UProject_JPlayerInputBindingComponent::UnbindInput()
{
	check(IsInGameThread());
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	const uint64 ReleaseRevision = ++BindingRevision;
	const auto PreviousInput = BoundEnhancedInputComponent;
	TStrongObjectPtr<AProject_JPlayerCharacter> PreviousPlayer(BoundPlayerCharacter.Get());
	const auto ReleasedHandles = MoveTemp(OwnedBindingHandles);
	const auto ReleasedInputs = MoveTemp(ActiveDirectInputTags);
	BoundEnhancedInputComponent.Reset();
	BoundPlayerCharacter = nullptr;
	ActiveSkillInputMappingData = nullptr;
	bPendingMoveStopReconciliation = bPendingSemanticMoveIntentRefresh = false;
	bSemanticMoveIntentActionsBound = false;
	SemanticMoveIntent.Clear();
	SetComponentTickEnabled(false);
	if (UEnhancedInputComponent* Input = PreviousInput.Get())
	{
		for (uint32 Handle : ReleasedHandles) Input->RemoveBindingByHandle(Handle);
	}
	if (PreviousPlayer.IsValid())
	{
		for (const auto& Entry : ReleasedInputs) PreviousPlayer->HandleSkillInputTagReleasedFromSource(Entry.Value.InputTag, this, Entry.Value.Receipt);
		// A callback may acquire a new device for this pawn. Its logical state now
		// belongs to that binding; old teardown must not reset the new router/move.
		if (!MayRetirePlayerState(PreviousPlayer.Get(), ReleaseRevision)) { return; }
		if (PreviousPlayer->SkillInputRouterComponent) PreviousPlayer->SkillInputRouterComponent->ResetInputState();
		if (!MayRetirePlayerState(PreviousPlayer.Get(), ReleaseRevision)) { return; }
		PreviousPlayer->StopJumping();
		PreviousPlayer->StopSprint();
		if (!MayRetirePlayerState(PreviousPlayer.Get(), ReleaseRevision)) { return; }
		FinalizeMoveStoppedForCharacter(PreviousPlayer.Get());
	}
}

bool UProject_JPlayerInputBindingComponent::MayRetirePlayerState(AProject_JPlayerCharacter* PlayerCharacter, uint64 ReleaseRevision) const
{
	return BindingRevision == ReleaseRevision || BoundPlayerCharacter != PlayerCharacter;
}

bool UProject_JPlayerInputBindingComponent::IsCurrentBinding(AProject_JPlayerCharacter* PlayerCharacter, uint64 Revision) const
{
	return !bEnding && !IsBeingDestroyed() && BindingRevision == Revision && BoundPlayerCharacter == PlayerCharacter &&
		IsValid(PlayerCharacter) && !PlayerCharacter->IsActorBeingDestroyed();
}

void UProject_JPlayerInputBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	UnbindInput();
	Super::EndPlay(EndPlayReason);
}
void UProject_JPlayerInputBindingComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEnding = true;
	UnbindInput();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JPlayerInputBindingComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPendingSemanticMoveIntentRefresh)
	{
		bPendingSemanticMoveIntentRefresh = false;
		RefreshSemanticMoveIntent();
	}

	if (bPendingMoveStopReconciliation)
	{
		// This tick runs after Enhanced Input has dispatched all mappings for the
		// frame. A non-zero Triggered Move callback clears this candidate before
		// we get here, so only a final zero-valued action becomes a semantic Stop.
		bPendingMoveStopReconciliation = false;
		FinalizeMoveStopped();
	}

	if (!bPendingMoveStopReconciliation && !bPendingSemanticMoveIntentRefresh)
	{
		SetComponentTickEnabled(false);
	}
}

void UProject_JPlayerInputBindingComponent::HandleInteract()
{
	if (BoundPlayerCharacter)
	{
		BoundPlayerCharacter->TryInteract();
	}
}

void UProject_JPlayerInputBindingComponent::HandleMove(const FInputActionValue& Value)
{
	check(IsInGameThread());
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TStrongObjectPtr<AProject_JPlayerCharacter> PinnedPlayer(BoundPlayerCharacter.Get());
	AProject_JPlayerCharacter* PlayerCharacter = PinnedPlayer.Get();
	const uint64 Revision = BindingRevision;
	if (!IsCurrentBinding(PlayerCharacter, Revision))
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (MoveInput.ContainsNaN()) { return; }
	if (Project_J::MotionMatchingCVars::ShouldTraceCombatStop() &&
		bPendingMoveStopReconciliation && MoveInput.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MMCombatStop Input=MoveResumed Actor=%s Frame=%llu Axis=(%.2f,%.2f) Speed=%.1f Intro=%d Outro=%d"),
			*GetNameSafe(PlayerCharacter), GFrameCounter, MoveInput.X, MoveInput.Y,
			PlayerCharacter->GetVelocity().Size2D(),
			PlayerCharacter->IsCombatIntroPlaying() ? 1 : 0,
			PlayerCharacter->IsCombatOutroPlaying() ? 1 : 0);
	}
	if (MoveInput.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		CancelPendingMoveStopReconciliation();
	}

	if (PlayerCharacter->LocomotionAnimStateComponent)
	{
		// Pivot has to observe the reversal on this movement update, before
		// CharacterMovement decelerates below its authored minimum speed.
		PlayerCharacter->LocomotionAnimStateComponent->SetMoveInput(MoveInput);
	}
	if (!IsCurrentBinding(PlayerCharacter, Revision)) { return; }

	// Sprint ability state affects CharacterMovement, so preserve its immediate
	// gameplay update as well.
	PlayerCharacter->UpdateSprintInputFromMove(MoveInput);
	if (!IsCurrentBinding(PlayerCharacter, Revision)) { return; }
	PlayerCharacter->UpdateMoveStartReplicationState(MoveInput);
	if (!IsCurrentBinding(PlayerCharacter, Revision)) { return; }

	if (AController* Controller = PlayerCharacter->GetController())
	{
		// find out which way is forward
		TStrongObjectPtr<AController> PinnedController(Controller);
		const FRotator Rotation = Controller->GetControlRotation();
		if (Rotation.ContainsNaN() || !IsCurrentBinding(PlayerCharacter, Revision) || PlayerCharacter->GetController() != Controller) { return; }
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement
		PlayerCharacter->AddMovementInput(ForwardDirection, MoveInput.Y);
		if (!IsCurrentBinding(PlayerCharacter, Revision) || PlayerCharacter->GetController() != Controller) { return; }
		PlayerCharacter->AddMovementInput(RightDirection, MoveInput.X);
	}
}

void UProject_JPlayerInputBindingComponent::HandleLook(const FInputActionValue& Value)
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (BoundPlayerCharacter->GetController() != nullptr)
	{
		BoundPlayerCharacter->AddControllerYawInput(LookAxisVector.X);
		BoundPlayerCharacter->AddControllerPitchInput(LookAxisVector.Y);
#if !UE_BUILD_SHIPPING
		BoundPlayerCharacter->RecordMouseTurnLookInput(LookAxisVector);
#endif
	}
}

void UProject_JPlayerInputBindingComponent::HandleMoveStopped()
{
	if (!BoundPlayerCharacter)
	{
		return;
	}
	if (Project_J::MotionMatchingCVars::ShouldTraceCombatStop())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MMCombatStop Input=MoveCompleted Actor=%s Frame=%llu Speed=%.1f Intro=%d Outro=%d PendingStop=%d"),
			*GetNameSafe(BoundPlayerCharacter.Get()), GFrameCounter,
			BoundPlayerCharacter->GetVelocity().Size2D(),
			BoundPlayerCharacter->IsCombatIntroPlaying() ? 1 : 0,
			BoundPlayerCharacter->IsCombatOutroPlaying() ? 1 : 0,
			bPendingMoveStopReconciliation ? 1 : 0);
	}

	// Do not translate an individual mapping's Completed/Canceled callback into
	// a Stop yet.  The same IA_Move can receive another non-zero mapping during
	// this Enhanced Input update (for example, holding A while pressing D).
	bPendingMoveStopReconciliation = true;
	SetComponentTickEnabled(true);
}

void UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStarted(const EProject_JMoveIntentDirection Direction)
{
	if (SemanticMoveIntent.Press(Direction)) { QueueSemanticMoveIntentRefresh(); }
}

void UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStopped(const EProject_JMoveIntentDirection Direction)
{
	if (SemanticMoveIntent.Release(Direction)) { QueueSemanticMoveIntentRefresh(); }
}

void UProject_JPlayerInputBindingComponent::QueueSemanticMoveIntentRefresh()
{
	if (!bSemanticMoveIntentActionsBound)
	{
		return;
	}

	bPendingSemanticMoveIntentRefresh = true;
	SetComponentTickEnabled(true);

	if (BoundPlayerCharacter && BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		BoundPlayerCharacter->LocomotionAnimStateComponent->BeginSemanticMoveIntentUpdate();
	}
}

void UProject_JPlayerInputBindingComponent::RefreshSemanticMoveIntent()
{
	if (!BoundPlayerCharacter || !BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		return;
	}

	const bool bHasActiveIntent = SemanticMoveIntent.HasActiveIntent();
	const FVector2D Intent = SemanticMoveIntent.GetIntent();
	BoundPlayerCharacter->LocomotionAnimStateComponent->SetSemanticMoveIntentInput(
		Intent, bHasActiveIntent);

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("CombatStrafeSemanticIntent Actor=%s PressRank[F=%d B=%d L=%d R=%d] Final=(%.2f,%.2f) Active=%s"),
			*GetNameSafe(BoundPlayerCharacter.Get()),
			SemanticMoveIntent.GetPressRank(EProject_JMoveIntentDirection::Forward), SemanticMoveIntent.GetPressRank(EProject_JMoveIntentDirection::Backward),
			SemanticMoveIntent.GetPressRank(EProject_JMoveIntentDirection::Left), SemanticMoveIntent.GetPressRank(EProject_JMoveIntentDirection::Right),
			Intent.X, Intent.Y,
			bHasActiveIntent ? TEXT("true") : TEXT("false"));
	}
}

void UProject_JPlayerInputBindingComponent::CancelPendingMoveStopReconciliation()
{
	bPendingMoveStopReconciliation = false;
}

void UProject_JPlayerInputBindingComponent::FinalizeMoveStopped()
{
	FinalizeMoveStoppedForCharacter(BoundPlayerCharacter.Get());
}

void UProject_JPlayerInputBindingComponent::FinalizeMoveStoppedForCharacter(AProject_JPlayerCharacter* PlayerCharacter)
{
	if (!PlayerCharacter)
	{
		return;
	}
	const uint64 ReleaseRevision = BindingRevision;

	const bool bHadMoveInput = PlayerCharacter->bHadMoveInputForReplication;
	const bool bWasSprintingAtStop =
		PlayerCharacter->IsSprintLocomotionAllowed() ||
		(PlayerCharacter->LocomotionAnimStateComponent &&
			(PlayerCharacter->LocomotionAnimStateComponent->bUseSprintLocomotion ||
			 PlayerCharacter->LocomotionAnimStateComponent->bWantsSprint));
	PlayerCharacter->ResetMoveStartReplicationState();

	if (PlayerCharacter->LocomotionAnimStateComponent)
	{
		PlayerCharacter->LocomotionAnimStateComponent->ClearMoveInput();
	}
	if (!MayRetirePlayerState(PlayerCharacter, ReleaseRevision)) { return; }
	if (Project_J::MotionMatchingCVars::ShouldTraceCombatStop())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MMCombatStop Input=StopApplied Actor=%s Frame=%llu Speed=%.1f HadMove=%d Sprint=%d StateInput=%d CachedAxis=%.2f"),
			*GetNameSafe(PlayerCharacter), GFrameCounter,
			PlayerCharacter->GetVelocity().Size2D(), bHadMoveInput ? 1 : 0,
			bWasSprintingAtStop ? 1 : 0,
			PlayerCharacter->LocomotionAnimStateComponent &&
				PlayerCharacter->LocomotionAnimStateComponent->bHasMoveInput ? 1 : 0,
			PlayerCharacter->LocomotionAnimStateComponent
				? PlayerCharacter->LocomotionAnimStateComponent->MoveInputSize : -1.0f);
	}

	PlayerCharacter->UpdateSprintInputFromMove(FVector2D::ZeroVector);
	if (!MayRetirePlayerState(PlayerCharacter, ReleaseRevision)) { return; }

	if (bHadMoveInput)
	{
		PlayerCharacter->DispatchMoveStopAnimationEvent(bWasSprintingAtStop);
	}
}

void UProject_JPlayerInputBindingComponent::HandleJumpStarted()
{
	if (!BoundPlayerCharacter) return;

	if (!BoundPlayerCharacter->IsJumpLocomotionAllowed())
	{
		return;
	}

	if (BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		if (!BoundPlayerCharacter->LocomotionAnimStateComponent->CanStartJumpForAnimation())
		{
			return;
		}

		BoundPlayerCharacter->LocomotionAnimStateComponent->HandleJumpStarted();
	}

	BoundPlayerCharacter->Jump();
}

void UProject_JPlayerInputBindingComponent::HandleJumpStopped()
{
	if (!BoundPlayerCharacter) return;
	BoundPlayerCharacter->StopJumping();
}

void UProject_JPlayerInputBindingComponent::HandlePrimarySkillPressed()
{
	if (BoundPlayerCharacter)
		if (const auto* PC = Cast<APlayerController>(BoundPlayerCharacter->GetController()); PC && PC->bShowMouseCursor) return;
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonPressed(EProject_JSkillInputButton::LMB);
	}
}

void UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonReleased(EProject_JSkillInputButton::LMB);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSecondarySkillPressed()
{
	if (BoundPlayerCharacter)
		if (const auto* PC = Cast<APlayerController>(BoundPlayerCharacter->GetController()); PC && PC->bShowMouseCursor) return;
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonPressed(EProject_JSkillInputButton::RMB);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonReleased(EProject_JSkillInputButton::RMB);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSkillModifierPressed()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->SetModifierHeld(true);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSkillModifierReleased()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->SetModifierHeld(false);
	}
}

void UProject_JPlayerInputBindingComponent::HandleDirectSkillActionPressed(UInputAction* InputAction)
{
	if (!InputAction || !BoundPlayerCharacter || !BoundPlayerCharacter->SkillInputRouterComponent || !ActiveSkillInputMappingData || ActiveDirectInputTags.Contains(InputAction))
	{
		return;
	}

	const FProject_JDirectSkillInputBinding* BestBinding = nullptr;
	for (const FProject_JDirectSkillInputBinding& SkillBinding : ActiveSkillInputMappingData->DirectSkillBindings)
	{
		if (SkillBinding.InputAction != InputAction || !SkillBinding.InputTag.IsValid() || !BoundPlayerCharacter->SkillInputRouterComponent->AreModifierTagsMatched(SkillBinding.RequiredModifierTags, SkillBinding.BlockedModifierTags))
		{
			continue;
		}

		if (!BestBinding || SkillBinding.Priority > BestBinding->Priority)
		{
			BestBinding = &SkillBinding;
		}
	}

	if (BestBinding)
	{
		if (++NextDirectInputReceipt == 0) ++NextDirectInputReceipt;
		ActiveDirectInputTags.Add(InputAction, {BestBinding->InputTag, NextDirectInputReceipt});
		BoundPlayerCharacter->HandleSkillInputTagPressedFromSource(BestBinding->InputTag, this, NextDirectInputReceipt);
	}
}

void UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased(UInputAction* InputAction)
{
	if (!InputAction || !BoundPlayerCharacter)
	{
		return;
	}

	FDirectInputPress Released;
	if (ActiveDirectInputTags.RemoveAndCopyValue(InputAction, Released))
	{
		BoundPlayerCharacter->HandleSkillInputTagReleasedFromSource(Released.InputTag, this, Released.Receipt);
	}
}
