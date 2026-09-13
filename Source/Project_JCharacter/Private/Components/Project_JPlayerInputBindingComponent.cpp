#include "Components/Project_JPlayerInputBindingComponent.h"

#include "EnhancedInputComponent.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "InputActionValue.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"

UProject_JPlayerInputBindingComponent::UProject_JPlayerInputBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

bool UProject_JPlayerInputBindingComponent::BindInput(UInputComponent* PlayerInputComponent, AProject_JPlayerCharacter* PlayerCharacter, const FProject_JPlayerInputActionSet& ActionSet)
{
	UnbindInput();
	BoundPlayerCharacter = PlayerCharacter;
	ActiveSkillInputMappingData = ActionSet.SkillInputMappingData;
	ActiveDirectInputTags.Reset();

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !BoundPlayerCharacter)
	{
		return false;
	}

	BoundEnhancedInputComponent = EnhancedInputComponent;
	const auto BindOwnedAction = [this, EnhancedInputComponent](auto Action, ETriggerEvent Event, auto* Object, auto Method, auto... Args)
	{
		const UInputAction* InputAction = Action;
		if (InputAction) OwnedBindingHandles.Add(EnhancedInputComponent->BindAction(InputAction, Event, Object, Method, Args...).GetHandle());
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
	bMoveIntentForwardHeld = false;
	bMoveIntentBackwardHeld = false;
	bMoveIntentLeftHeld = false;
	bMoveIntentRightHeld = false;
	MoveIntentPressSequence = 0;
	ForwardPressSequence = 0;
	BackwardPressSequence = 0;
	LeftPressSequence = 0;
	RightPressSequence = 0;

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
	if (UEnhancedInputComponent* Input = BoundEnhancedInputComponent.Get())
	{
		for (uint32 Handle : OwnedBindingHandles) Input->RemoveBindingByHandle(Handle);
	}
	OwnedBindingHandles.Reset();
	BoundEnhancedInputComponent.Reset();
	const auto ReleasedInputs = MoveTemp(ActiveDirectInputTags);
	if (IsValid(BoundPlayerCharacter))
	{
		for (const auto& Entry : ReleasedInputs) BoundPlayerCharacter->HandleSkillInputTagReleased(Entry.Value);
		if (BoundPlayerCharacter->SkillInputRouterComponent) BoundPlayerCharacter->SkillInputRouterComponent->ResetInputState();
		HandleJumpStopped();
		BoundPlayerCharacter->StopSprint();
		FinalizeMoveStopped();
	}
	bPendingMoveStopReconciliation = bPendingSemanticMoveIntentRefresh = false;
	bSemanticMoveIntentActionsBound = false;
	bMoveIntentForwardHeld = bMoveIntentBackwardHeld = bMoveIntentLeftHeld = bMoveIntentRightHeld = false;
	SetComponentTickEnabled(false);
	ActiveSkillInputMappingData = nullptr;
	BoundPlayerCharacter = nullptr;
}

void UProject_JPlayerInputBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInput();
	Super::EndPlay(EndPlayReason);
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
	if (!BoundPlayerCharacter)
	{
		return;
	}

	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (MoveInput.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		CancelPendingMoveStopReconciliation();
	}

	if (BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		// Pivot has to observe the reversal on this movement update, before
		// CharacterMovement decelerates below its authored minimum speed.
		BoundPlayerCharacter->LocomotionAnimStateComponent->SetMoveInput(MoveInput);
	}

	// Sprint ability state affects CharacterMovement, so preserve its immediate
	// gameplay update as well.
	BoundPlayerCharacter->UpdateSprintInputFromMove(MoveInput);
	BoundPlayerCharacter->UpdateMoveStartReplicationState(MoveInput);

	if (BoundPlayerCharacter->GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = BoundPlayerCharacter->GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement
		BoundPlayerCharacter->AddMovementInput(ForwardDirection, MoveInput.Y);
		BoundPlayerCharacter->AddMovementInput(RightDirection, MoveInput.X);
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
	}
}

void UProject_JPlayerInputBindingComponent::HandleMoveStopped()
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	// Do not translate an individual mapping's Completed/Canceled callback into
	// a Stop yet.  The same IA_Move can receive another non-zero mapping during
	// this Enhanced Input update (for example, holding A while pressing D).
	bPendingMoveStopReconciliation = true;
	SetComponentTickEnabled(true);
}

void UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStarted(const EProject_JMoveIntentDirection Direction)
{
	bool* HeldState = nullptr;
	int32* PressSequence = nullptr;
	switch (Direction)
	{
	case EProject_JMoveIntentDirection::Forward:
		HeldState = &bMoveIntentForwardHeld;
		PressSequence = &ForwardPressSequence;
		break;
	case EProject_JMoveIntentDirection::Backward:
		HeldState = &bMoveIntentBackwardHeld;
		PressSequence = &BackwardPressSequence;
		break;
	case EProject_JMoveIntentDirection::Left:
		HeldState = &bMoveIntentLeftHeld;
		PressSequence = &LeftPressSequence;
		break;
	case EProject_JMoveIntentDirection::Right:
		HeldState = &bMoveIntentRightHeld;
		PressSequence = &RightPressSequence;
		break;
	default:
		return;
	}

	if (*HeldState)
	{
		return;
	}

	*HeldState = true;
	++MoveIntentPressSequence;
	if (MoveIntentPressSequence == 0)
	{
		MoveIntentPressSequence = 1;
	}
	*PressSequence = MoveIntentPressSequence;
	QueueSemanticMoveIntentRefresh();
}

void UProject_JPlayerInputBindingComponent::HandleMoveIntentDirectionStopped(const EProject_JMoveIntentDirection Direction)
{
	bool* HeldState = nullptr;
	switch (Direction)
	{
	case EProject_JMoveIntentDirection::Forward:
		HeldState = &bMoveIntentForwardHeld;
		break;
	case EProject_JMoveIntentDirection::Backward:
		HeldState = &bMoveIntentBackwardHeld;
		break;
	case EProject_JMoveIntentDirection::Left:
		HeldState = &bMoveIntentLeftHeld;
		break;
	case EProject_JMoveIntentDirection::Right:
		HeldState = &bMoveIntentRightHeld;
		break;
	default:
		return;
	}

	if (!*HeldState)
	{
		return;
	}

	*HeldState = false;
	QueueSemanticMoveIntentRefresh();
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

	const bool bHasHorizontalIntent = bMoveIntentLeftHeld || bMoveIntentRightHeld;
	const bool bHasVerticalIntent = bMoveIntentForwardHeld || bMoveIntentBackwardHeld;
	const bool bHasActiveIntent = bHasHorizontalIntent || bHasVerticalIntent;

	float Horizontal = 0.0f;
	if (bMoveIntentLeftHeld && bMoveIntentRightHeld)
	{
		// Explicit semantic policy for opposed keys: the latest pressed direction
		// wins. This mirrors the player's meaningful edge without querying keys.
		Horizontal = RightPressSequence >= LeftPressSequence ? 1.0f : -1.0f;
	}
	else if (bMoveIntentRightHeld)
	{
		Horizontal = 1.0f;
	}
	else if (bMoveIntentLeftHeld)
	{
		Horizontal = -1.0f;
	}

	float Vertical = 0.0f;
	if (bMoveIntentForwardHeld && bMoveIntentBackwardHeld)
	{
		Vertical = ForwardPressSequence >= BackwardPressSequence ? 1.0f : -1.0f;
	}
	else if (bMoveIntentForwardHeld)
	{
		Vertical = 1.0f;
	}
	else if (bMoveIntentBackwardHeld)
	{
		Vertical = -1.0f;
	}

	BoundPlayerCharacter->LocomotionAnimStateComponent->SetSemanticMoveIntentInput(
		FVector2D(Horizontal, Vertical).GetSafeNormal(), bHasActiveIntent);

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("CombatStrafeSemanticIntent Actor=%s Held[F=%s B=%s L=%s R=%s] PressSeq[F=%d B=%d L=%d R=%d] Final=(%.2f,%.2f) Active=%s"),
			*GetNameSafe(BoundPlayerCharacter.Get()),
			bMoveIntentForwardHeld ? TEXT("true") : TEXT("false"),
			bMoveIntentBackwardHeld ? TEXT("true") : TEXT("false"),
			bMoveIntentLeftHeld ? TEXT("true") : TEXT("false"),
			bMoveIntentRightHeld ? TEXT("true") : TEXT("false"),
			ForwardPressSequence, BackwardPressSequence, LeftPressSequence, RightPressSequence,
			Horizontal, Vertical,
			bHasActiveIntent ? TEXT("true") : TEXT("false"));
	}
}

void UProject_JPlayerInputBindingComponent::CancelPendingMoveStopReconciliation()
{
	bPendingMoveStopReconciliation = false;
}

void UProject_JPlayerInputBindingComponent::FinalizeMoveStopped()
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	const bool bHadMoveInput = BoundPlayerCharacter->bHadMoveInputForReplication;
	const bool bWasSprintingAtStop =
		BoundPlayerCharacter->IsSprintLocomotionAllowed() ||
		(BoundPlayerCharacter->LocomotionAnimStateComponent &&
			(BoundPlayerCharacter->LocomotionAnimStateComponent->bUseSprintLocomotion ||
			 BoundPlayerCharacter->LocomotionAnimStateComponent->bWantsSprint));
	BoundPlayerCharacter->ResetMoveStartReplicationState();

	if (BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		BoundPlayerCharacter->LocomotionAnimStateComponent->ClearMoveInput();
	}

	BoundPlayerCharacter->UpdateSprintInputFromMove(FVector2D::ZeroVector);

	if (bHadMoveInput)
	{
		BoundPlayerCharacter->DispatchMoveStopAnimationEvent(bWasSprintingAtStop);
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
	if (!InputAction || !BoundPlayerCharacter || !BoundPlayerCharacter->SkillInputRouterComponent || !ActiveSkillInputMappingData)
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
		ActiveDirectInputTags.Add(InputAction, BestBinding->InputTag);
		BoundPlayerCharacter->HandleSkillInputTagPressed(BestBinding->InputTag);
	}
}

void UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased(UInputAction* InputAction)
{
	if (!InputAction || !BoundPlayerCharacter)
	{
		return;
	}

	FGameplayTag ReleasedTag;
	if (ActiveDirectInputTags.RemoveAndCopyValue(InputAction, ReleasedTag))
	{
		BoundPlayerCharacter->HandleSkillInputTagReleased(ReleasedTag);
	}
}
