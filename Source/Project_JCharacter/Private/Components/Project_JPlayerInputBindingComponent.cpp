#include "Components/Project_JPlayerInputBindingComponent.h"

#include "EnhancedInputComponent.h"
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
	BoundPlayerCharacter = PlayerCharacter;
	ActiveSkillInputMappingData = ActionSet.SkillInputMappingData;
	ActiveDirectInputTags.Reset();

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !BoundPlayerCharacter)
	{
		return false;
	}

	EnhancedInputComponent->BindAction(ActionSet.JumpAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleJumpStarted);
	EnhancedInputComponent->BindAction(ActionSet.JumpAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleJumpStopped);

	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleMove);
	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);
	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);

	EnhancedInputComponent->BindAction(ActionSet.MouseLookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);
	EnhancedInputComponent->BindAction(ActionSet.LookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);

	EnhancedInputComponent->BindAction(ActionSet.SprintAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StartSprint);
	EnhancedInputComponent->BindAction(ActionSet.SprintAction, ETriggerEvent::Completed, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StopSprint);
	EnhancedInputComponent->BindAction(ActionSet.ToggleCombatAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::ToggleCombatMode);
	if (ActionSet.AttackAction)
	{
		EnhancedInputComponent->BindAction(ActionSet.AttackAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillPressed);
		EnhancedInputComponent->BindAction(ActionSet.AttackAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased);
		EnhancedInputComponent->BindAction(ActionSet.AttackAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased);
	}
	if (ActionSet.HeavyAttackAction)
	{
		EnhancedInputComponent->BindAction(ActionSet.HeavyAttackAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillPressed);
		EnhancedInputComponent->BindAction(ActionSet.HeavyAttackAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased);
		EnhancedInputComponent->BindAction(ActionSet.HeavyAttackAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased);
	}
	if (ActionSet.SkillModifierAction)
	{
		EnhancedInputComponent->BindAction(ActionSet.SkillModifierAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierPressed);
		EnhancedInputComponent->BindAction(ActionSet.SkillModifierAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierReleased);
		EnhancedInputComponent->BindAction(ActionSet.SkillModifierAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleSkillModifierReleased);
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
				EnhancedInputComponent->BindAction(ModifierBinding.InputAction, ETriggerEvent::Started, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierPressed, ModifierBinding.ModifierTag);
				EnhancedInputComponent->BindAction(ModifierBinding.InputAction, ETriggerEvent::Completed, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierReleased, ModifierBinding.ModifierTag);
				EnhancedInputComponent->BindAction(ModifierBinding.InputAction, ETriggerEvent::Canceled, SkillInputRouter, &UProject_JSkillInputRouterComponent::HandleModifierReleased, ModifierBinding.ModifierTag);
			}
		}

		for (const FProject_JDirectSkillInputBinding& SkillBinding : SkillInputMappingData->DirectSkillBindings)
		{
			if (!SkillBinding.InputAction || !SkillBinding.InputTag.IsValid() || BoundSkillActions.Contains(SkillBinding.InputAction))
			{
				continue;
			}

			BoundSkillActions.Add(SkillBinding.InputAction);
			EnhancedInputComponent->BindAction(SkillBinding.InputAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionPressed, SkillBinding.InputAction.Get());
			EnhancedInputComponent->BindAction(SkillBinding.InputAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased, SkillBinding.InputAction.Get());
			EnhancedInputComponent->BindAction(SkillBinding.InputAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleDirectSkillActionReleased, SkillBinding.InputAction.Get());
		}
	}

	if (ActionSet.InteractAction)
	{
		EnhancedInputComponent->BindAction(ActionSet.InteractAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleInteract);
	}

	return true;
}

void UProject_JPlayerInputBindingComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPendingMoveStopReconciliation)
	{
		// This tick runs after Enhanced Input has dispatched all mappings for the
		// frame. A non-zero Triggered Move callback clears this candidate before
		// we get here, so only a final zero-valued action becomes a semantic Stop.
		bPendingMoveStopReconciliation = false;
		FinalizeMoveStopped();
	}

	if (!bPendingMoveStopReconciliation)
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

	if (FGameplayTag* ActiveInputTag = ActiveDirectInputTags.Find(InputAction))
	{
		BoundPlayerCharacter->HandleSkillInputTagReleased(*ActiveInputTag);
		ActiveDirectInputTags.Remove(InputAction);
	}
}
