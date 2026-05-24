#pragma once

#include "CoreMinimal.h"
#include "NetworkPredictionComponent.h"
#include "NetworkPredictionStateTypes.h"
#include "Project_JNetworkMovementComponent.generated.h"

// ------------------------------------------------------------------------------------------------
// NPP Data Structures: Command (Input) and SyncState (Position/Velocity)
// ------------------------------------------------------------------------------------------------

USTRUCT()
struct FProject_JCharacterMotionInputCmd
{
	GENERATED_BODY()

	FVector MovementInput = FVector::ZeroVector;
	bool bIsJumping = false;
	
	void NetSerialize(const FNetSerializeParams& P)
	{
		// Serialize input over network
	}
};

USTRUCT()
struct FProject_JCharacterMotionSyncState
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	void NetSerialize(const FNetSerializeParams& P)
	{
		// Serialize state over network
	}
};

USTRUCT()
struct FProject_JCharacterMotionAuxState
{
	GENERATED_BODY()
	// Auxiliary data for interpolation/cosmetic updates
};

/**
 * Custom Movement Component powered by Network Prediction Plugin (NPP).
 * Replaces the legacy CharacterMovementComponent to provide perfect rollback,
 * client prediction, and potential support for mounts and vehicles.
 */
UCLASS(meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JNetworkMovementComponent : public UNetworkPredictionComponent
{
	GENERATED_BODY()

public:
	UProject_JNetworkMovementComponent();

protected:
	virtual void InitializeNetworkPredictionProxy() override;
};
