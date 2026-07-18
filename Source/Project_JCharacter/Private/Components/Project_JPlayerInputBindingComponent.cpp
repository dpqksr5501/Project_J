#include "Components/Project_JPlayerInputBindingComponent.h"

#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"

UProject_JPlayerInputBindingComponent::UProject_JPlayerInputBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UProject_JPlayerInputBindingComponent::BindInput(UInputComponent* PlayerInputComponent, AProject_JPlayerCharacter* PlayerCharacter, const FProject_JPlayerInputActionSet& ActionSet)
{
	BoundPlayerCharacter = PlayerCharacter;

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
	if (ActionSet.InteractAction)
	{
		EnhancedInputComponent->BindAction(ActionSet.InteractAction, ETriggerEvent::Started, this, &UProject_JPlayerInputBindingComponent::HandleInteract);
	}

	return true;
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

	if (BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		BoundPlayerCharacter->LocomotionAnimStateComponent->SetMoveInput(MoveInput);
	}

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

	const bool bHadMoveInput = BoundPlayerCharacter->bHadMoveInputForReplication;
	BoundPlayerCharacter->ResetMoveStartReplicationState();

	if (BoundPlayerCharacter->LocomotionAnimStateComponent)
	{
		BoundPlayerCharacter->LocomotionAnimStateComponent->ClearMoveInput();
	}

	if (bHadMoveInput)
	{
		BoundPlayerCharacter->DispatchMoveStopAnimationEvent();
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
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonPressed(EProject_JSkillInputButton::Primary);
	}
}

void UProject_JPlayerInputBindingComponent::HandlePrimarySkillReleased()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonReleased(EProject_JSkillInputButton::Primary);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSecondarySkillPressed()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonPressed(EProject_JSkillInputButton::Secondary);
	}
}

void UProject_JPlayerInputBindingComponent::HandleSecondarySkillReleased()
{
	if (BoundPlayerCharacter && BoundPlayerCharacter->SkillInputRouterComponent)
	{
		BoundPlayerCharacter->SkillInputRouterComponent->HandleButtonReleased(EProject_JSkillInputButton::Secondary);
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
