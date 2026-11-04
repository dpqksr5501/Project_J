// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "DefaultMovementSet/Modes/SmoothWalkingState.h"

#include "MoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/GroundMovementUtils.h"
#include "NetworkPredictionWorldManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmoothWalkingMode)

// Movers that don't run local simulation ticks (e.g. interpolated network sim proxies) never generate moves,
// so the DidGenerateMove staleness flag is meaningless for them and their replicated spring state must be
// trusted as received.
static inline bool IsSmoothWalkingMoverSimulatingLocally(const UMoverComponent* MoverComp)
{
	if (MoverComp && MoverComp->GetOwnerRole() == ROLE_SimulatedProxy && UNetworkPredictionWorldManager::ActiveInstance)
	{
		return UNetworkPredictionWorldManager::ActiveInstance->GetSettings().SimulatedProxyNetworkLOD != ENetworkLOD::Interpolated;
	}

	return true;
}

void USmoothWalkingMode::MarkPredictionGenerated(const FMoverSimContext& SimContext, bool& bOutInitializeSubsequentSpring) const
{
	SimContext.Blackboard.TrySet(DidGenerateMoveEntry, true);
	// An entry may exist while its predictive timestamp is unreadable (for example
	// before a first real simulation frame). Mirror the next native TryGet rather
	// than assuming that entry existence or successful TrySet implies a receipt.
	bool bDidGenerate = false;
	SimContext.Blackboard.TryGet(DidGenerateMoveEntry, bDidGenerate);
	bOutInitializeSubsequentSpring = !bDidGenerate && IsSmoothWalkingMoverSimulatingLocally(GetMoverComponent());
}

void USmoothWalkingMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	Super::SimulationTick_Implementation(Params, OutputState);

	// If we've created or updated the spring state during GenerateWalkMove, we need to copy it into the output simulation state. 
	if (const FSmoothWalkingState* InSpringState = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FSmoothWalkingState>())
	{
		FSmoothWalkingState& OutputSpringState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FSmoothWalkingState>();
		OutputSpringState = *InSpringState;
	}
}

void USmoothWalkingMode::GenerateWalkMove_Implementation(FMoverTickStartData& StartState, float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity) 
{
	if (DeltaSeconds <= FLT_EPSILON)
	{
		return;
	}
	
	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	if (!ensure(StartingSyncState))
	{
		return;
	}

	// Find or add a FSmoothWalkingState to the SyncState
	bool bIsSmoothWalkingStateNew = false;
	FSmoothWalkingState& SpringState = StartState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FSmoothWalkingState>(bIsSmoothWalkingStateNew);

	bool bDidGenerateMoveSinceLastFrame = false;
	SimContext.Blackboard.TryGet(DidGenerateMoveEntry, bDidGenerateMoveSinceLastFrame);

	FSmoothWalkingMoveParams Params;
	Params.bInitializeSpring = bIsSmoothWalkingStateNew || (!bDidGenerateMoveSinceLastFrame && IsSmoothWalkingMoverSimulatingLocally(GetMoverComponent()));
	Params.DeltaSeconds = DeltaSeconds;
	Params.DesiredVelocity = DesiredVelocity;
	Params.DesiredFacing = DesiredFacing;
	Params.CurrentFacing = CurrentFacing;
	CaptureSmoothWalkingSettings(Params);
	FSmoothWalkingStateView StateView{SpringState.SpringVelocity, SpringState.SpringAcceleration,
		SpringState.IntermediateVelocity, SpringState.IntermediateFacing, SpringState.IntermediateAngularVelocity};
	UGroundMovementUtils::ComputeSmoothWalkingMove(Params, StateView, InOutAngularVelocityDegrees, InOutVelocity);
}



void USmoothWalkingMode::CaptureSmoothWalkingSettings(FSmoothWalkingMoveParams& OutParams) const
{
	OutParams.Acceleration = Acceleration;
	OutParams.Deceleration = Deceleration;
	OutParams.DirectionalAccelerationFactor = DirectionalAccelerationFactor;
	OutParams.TurningStrength = TurningStrength;
	OutParams.AccelerationSmoothingTime = AccelerationSmoothingTime;
	OutParams.DecelerationSmoothingTime = DecelerationSmoothingTime;
	OutParams.AccelerationSmoothingCompensation = AccelerationSmoothingCompensation;
	OutParams.DecelerationSmoothingCompensation = DecelerationSmoothingCompensation;
	OutParams.VelocityDeadzoneThreshold = VelocityDeadzoneThreshold;
	OutParams.AccelerationDeadzoneThreshold = AccelerationDeadzoneThreshold;
	OutParams.OutsideInfluenceSmoothingTime = OutsideInfluenceSmoothingTime;
	OutParams.FacingSmoothingTime = FacingSmoothingTime;
	OutParams.FacingDeadzoneThreshold = FacingDeadzoneThreshold;
	OutParams.AngularVelocityDeadzoneThreshold = AngularVelocityDeadzoneThreshold;
	OutParams.bSmoothFacingWithDoubleSpring = bSmoothFacingWithDoubleSpring;
}

bool USmoothWalkingMode::CapturePredictionState(const FMoverSyncState& StartSync, const FMoverSimContext& SimContext,
	FSmoothWalkingStateValues& OutState, bool& bOutInitializeSpring) const
{
	// This is a live GT capture, not a worker entry point or a state/blackboard writer.
	const UMoverComponent* Mover = GetMoverComponent();
	if (!IsInGameThread() || IsTemplate() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || !IsValid(Mover) || Mover->IsBeingDestroyed()) { return false; }
	if (!StartSync.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>()) { return false; }
	const FSmoothWalkingState* Spring = StartSync.SyncStateCollection.FindDataByType<FSmoothWalkingState>();
	FSmoothWalkingStateValues Captured;
	const bool bHasSpring = Spring != nullptr;
	if (Spring)
	{
		Captured.SpringVelocity = Spring->SpringVelocity;
		Captured.SpringAcceleration = Spring->SpringAcceleration;
		Captured.IntermediateVelocity = Spring->IntermediateVelocity;
		Captured.IntermediateFacing = Spring->IntermediateFacing;
		Captured.IntermediateAngularVelocity = Spring->IntermediateAngularVelocity;
	}
	bool bDidGenerate = false;
	SimContext.Blackboard.TryGet(DidGenerateMoveEntry, bDidGenerate);
	// Preserve new || (!did && role) ordering and short circuit for the first step.
	const bool bInitialize = !bHasSpring || (!bDidGenerate && IsSmoothWalkingMoverSimulatingLocally(Mover));
	OutState = Captured;
	bOutInitializeSpring = bInitialize;
	return true;
}
