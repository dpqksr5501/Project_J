#include "Components/Project_JCharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"

void UProject_JCharacterMovementComponent::ApplyMovePolicy(const FVector& Direction)
{
	if (auto* Player = Cast<AProject_JPlayerCharacter>(CharacterOwner))
	{
		// A compressed flag never grants an ability or bypasses server combat rules.
		const bool bAllowed = Player->HasAuthority()
			? Player->IsSprintLocomotionAllowed() : !Player->IsCombatActionBlockingSprint();
		Player->ApplyMovementPolicy(bSprintRequestedForMove && bAllowed, Direction);
	}
}

void UProject_JCharacterMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	if (const auto* Player = Cast<AProject_JPlayerCharacter>(CharacterOwner))
		bSprintRequestedForMove = Player->IsSprintLocomotionAllowed();
	ApplyMovePolicy(InputVector); // Before CMC scales/clamps input acceleration.
	Super::ControlledCharacterMove(InputVector, DeltaSeconds);
}

void UProject_JCharacterMovementComponent::MoveAutonomous(float TimeStamp, float DeltaTime, uint8 Flags, const FVector& NewAccel)
{
	UpdateFromCompressedFlags(Flags);
	ApplyMovePolicy(NewAccel); // Server and correction replay must clamp with this move's acceleration limit.
	Super::MoveAutonomous(TimeStamp, DeltaTime, Flags, NewAccel);
}

void UProject_JCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bSprintRequestedForMove = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

void UProject_JCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	ApplyMovePolicy(Acceleration);
}

FNetworkPredictionData_Client* UProject_JCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
		const_cast<UProject_JCharacterMovementComponent*>(this)->ClientPredictionData = new FProject_JClientPredictionData(*this);
	return ClientPredictionData;
}

void FProject_JSavedMove::Clear()
{
	Super::Clear();
	bSavedSprint = false;
}
uint8 FProject_JSavedMove::GetCompressedFlags() const
{
	return Super::GetCompressedFlags() | (bSavedSprint ? FLAG_Custom_0 : 0);
}
bool FProject_JSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	if (bSavedSprint != static_cast<const FProject_JSavedMove*>(NewMove.Get())->bSavedSprint) return false;
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}
void FProject_JSavedMove::SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	const auto* Movement = Cast<UProject_JCharacterMovementComponent>(Character->GetCharacterMovement());
	bSavedSprint = Movement && Movement->IsSprintRequestedForMove();
}
void FProject_JSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	if (auto* Movement = Cast<UProject_JCharacterMovementComponent>(Character->GetCharacterMovement()))
		Movement->UpdateFromCompressedFlags(GetCompressedFlags());
}
