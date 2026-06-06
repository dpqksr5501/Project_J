#include "Components/Project_JPlayerInputBindingComponent.h"

#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Project_JPlayerCharacter.h"

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

	EnhancedInputComponent->BindAction(ActionSet.JumpAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::DoJumpStart);
	EnhancedInputComponent->BindAction(ActionSet.JumpAction, ETriggerEvent::Completed, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::DoJumpEnd);

	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleMove);
	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Completed, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);
	EnhancedInputComponent->BindAction(ActionSet.MoveAction, ETriggerEvent::Canceled, this, &UProject_JPlayerInputBindingComponent::HandleMoveStopped);

	EnhancedInputComponent->BindAction(ActionSet.MouseLookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);
	EnhancedInputComponent->BindAction(ActionSet.LookAction, ETriggerEvent::Triggered, this, &UProject_JPlayerInputBindingComponent::HandleLook);

	EnhancedInputComponent->BindAction(ActionSet.SprintAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StartSprint);
	EnhancedInputComponent->BindAction(ActionSet.SprintAction, ETriggerEvent::Completed, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::StopSprint);
	EnhancedInputComponent->BindAction(ActionSet.ToggleCombatAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::ToggleCombatMode);
	EnhancedInputComponent->BindAction(ActionSet.AttackAction, ETriggerEvent::Started, BoundPlayerCharacter.Get(), &AProject_JPlayerCharacter::TriggerPlayerAttack);

	return true;
}

void UProject_JPlayerInputBindingComponent::HandleMove(const FInputActionValue& Value)
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();
	BoundPlayerCharacter->DoMove(MovementVector.X, MovementVector.Y);
}

void UProject_JPlayerInputBindingComponent::HandleLook(const FInputActionValue& Value)
{
	if (!BoundPlayerCharacter)
	{
		return;
	}

	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	BoundPlayerCharacter->DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void UProject_JPlayerInputBindingComponent::HandleMoveStopped()
{
	if (BoundPlayerCharacter)
	{
		BoundPlayerCharacter->StopMoveInput();
	}
}
