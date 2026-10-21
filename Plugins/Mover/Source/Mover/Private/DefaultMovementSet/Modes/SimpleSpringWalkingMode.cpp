// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/Modes/SimpleSpringWalkingMode.h"
#include "DefaultMovementSet/Modes/SimpleSpringState.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Math/SpringMath.h"
#include "MoveLibrary/RollbackBlackboard.h"
#include "NetworkPredictionWorldManager.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleSpringWalkingMode)


// Movers that don't run local simulation ticks (e.g. interpolated network sim proxies) never generate moves,
// so the DidGenerateMove staleness flag is meaningless for them and their replicated spring state must be
// trusted as received.
static inline bool IsSimpleSpringWalkingMoverSimulatingLocally(const UMoverComponent* MoverComp)
{
	if (MoverComp && MoverComp->GetOwnerRole() == ROLE_SimulatedProxy && UNetworkPredictionWorldManager::ActiveInstance)
	{
		return UNetworkPredictionWorldManager::ActiveInstance->GetSettings().SimulatedProxyNetworkLOD != ENetworkLOD::Interpolated;
	}

	return true;
}



void USimpleSpringWalkingMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	Super::SimulationTick_Implementation(Params, OutputState);

	// If we've created or updated the spring state during GenerateMove, we need to copy it into the output simulation state.
	if (const FSimpleSpringState* InSpringState = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FSimpleSpringState>())
	{
		FSimpleSpringState& OutputSpringState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FSimpleSpringState>();
		OutputSpringState = *InSpringState;
	}
}

void USimpleSpringWalkingMode::GenerateWalkMove_Implementation(FMoverTickStartData& StartState, float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
	bool bIsSpringStateNew = false;
	FSimpleSpringState& SpringState = StartState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FSimpleSpringState>(bIsSpringStateNew);


	// If the spring state is stale, we need to reset its internal state
	if (!bIsSpringStateNew && IsSimpleSpringWalkingMoverSimulatingLocally(GetMoverComponent()))
	{
		bool bDidGenerateMoveSinceLastFrame = false;
		SimContext.Blackboard.TryGet(DidGenerateMoveEntry, bDidGenerateMoveSinceLastFrame);
		if (!bDidGenerateMoveSinceLastFrame)
		{
			SpringState = FSimpleSpringState();
		}
	}

	// Linear //
	
	SpringMath::CriticalSpringDamper(InOutVelocity, SpringState.CurrentAccel, DesiredVelocity, VelocitySmoothingTime, DeltaSeconds);
	
	// Angular //
	
	FVector CurrentAngularVelocityRad = FMath::DegreesToRadians(InOutAngularVelocityDegrees);
	FQuat UpdatedFacing = CurrentFacing;
	SpringMath::CriticalSpringDamperQuat(UpdatedFacing, CurrentAngularVelocityRad, DesiredFacing, FacingSmoothingTime, DeltaSeconds);
	InOutAngularVelocityDegrees = FMath::RadiansToDegrees(CurrentAngularVelocityRad);
}

