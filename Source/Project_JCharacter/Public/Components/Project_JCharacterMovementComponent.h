#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JCharacterMovementComponent.generated.h"

/** Sprint intent travels with the move, rather than an unrelated actor tick. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	bool IsSprintRequestedForMove() const { return bSprintRequestedForMove; }
private:
	void ApplyMovePolicy(const FVector& Direction);
	bool bSprintRequestedForMove = false;
};

class PROJECT_JCHARACTER_API FProject_JSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
	bool bSavedSprint = false;
};

class FProject_JClientPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	explicit FProject_JClientPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FProject_JSavedMove()); }
};
