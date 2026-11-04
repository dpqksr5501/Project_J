// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultMovementSet/Modes/SimpleWalkingMode.h"

#include "MoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/RollbackBlackboardLibrary.h"
#include "MoveLibrary/GroundMovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SimpleWalkingMode)

const FName USimpleWalkingMode::DidGenerateMoveEntry = TEXT("DidGenerateMove");


void USimpleWalkingMode::OnRegistered(const FName ModeName, const FMoverSimContext& SimContext)
{
	Super::OnRegistered(ModeName, SimContext);

	URollbackBlackboard::EntrySettings DidGenerateMoveEntrySettings = URollbackBlackboardLibrary::MakeSingleFrameEntrySettings();
	DidGenerateMoveEntrySettings.PersistencePolicy = EBlackboardPersistencePolicy::ThroughNextFrame;

	SimContext.Blackboard.CreateEntry<bool>(DidGenerateMoveEntry, DidGenerateMoveEntrySettings);
}


void USimpleWalkingMode::GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
                                                   FProposedMove& OutProposedMove) const
{
	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();

	if (!StartingSyncState)
	{
		return;
	}

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	if (DeltaSeconds <= FLT_EPSILON)
	{
		return;
	}

	// Get input
	FVector DesiredVelocity;
	EMoveInputType MoveInputType;
	FVector DesiredFacingDir;
	 
	if (const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
	{
		DesiredVelocity = CharacterInputs->GetMoveInput_WorldSpace();
		MoveInputType = CharacterInputs->GetMoveInputType();
		DesiredFacingDir = CharacterInputs->GetOrientationIntentDir_WorldSpace();
	}
	else
	{
		// If no input found, most likely a networked sim proxy
		// Try to deduce an input from the sync state
		DesiredVelocity = StartingSyncState->GetIntent_WorldSpace();
		MoveInputType = EMoveInputType::DirectionalIntent;
		DesiredFacingDir = StartingSyncState->GetOrientation_WorldSpace().Quaternion().GetForwardVector();
	}

	const bool bHasMaxMoveSpeed = MaxSpeedOverride >= 0.0f || CommonLegacySettings;
	float MaxMoveSpeed = MaxSpeedOverride >= 0.0f ? MaxSpeedOverride : (CommonLegacySettings ? CommonLegacySettings->MaxSpeed : 0.0f);

	const UMoverComponent* MoverComponent = GetMoverComponent();
	const FVector UpVector = MoverComponent ? MoverComponent->GetUpDirection() : FVector::UpVector;

	FSimpleWalkingInputParams InputParams;
	InputParams.MoveInput = DesiredVelocity;
	InputParams.FacingIntent = DesiredFacingDir;
	InputParams.MoveInputType = MoveInputType;
	InputParams.UpDirection = UpVector;
	InputParams.CurrentFacing = StartingSyncState->GetOrientation_WorldSpace().Quaternion();
	InputParams.MaxMoveSpeed = MaxMoveSpeed;
	InputParams.bHasMaxMoveSpeed = bHasMaxMoveSpeed;
	const FSimpleWalkingInputResult Projected = UGroundMovementUtils::ComputeSimpleWalkingInput(InputParams);
	OutProposedMove.DirectionIntent = Projected.DirectionIntent;
	OutProposedMove.bHasDirIntent = Projected.bHasDirIntent;
	DesiredVelocity = Projected.DesiredVelocity;
	const FQuat CurrentFacing = InputParams.CurrentFacing;
	const FQuat DesiredFacing = Projected.DesiredFacing;
	// Diagnostics belong to the live caller, not to the reusable value-only projection.
	if (MoveInputType != EMoveInputType::DirectionalIntent && MoveInputType != EMoveInputType::Velocity)
	{
		UE_LOGF(LogMover, Warning, "Unhandled MoveInputType %i in USimpleWalkingMode", EnumToUnderlyingType(MoveInputType));
	}

	OutProposedMove.LinearVelocity = StartingSyncState->GetVelocity_WorldSpace();
	OutProposedMove.AngularVelocityDegrees = StartingSyncState->GetAngularVelocityDegrees_WorldSpace();
	
	// Hack const_cast stuff
	// Why is this needed?
	// Because some modes want to mutate their data inside the generate walk move
	USimpleWalkingMode* MutableSimpleWalkMode  = const_cast<USimpleWalkingMode*>(this);
	MutableSimpleWalkMode->GenerateWalkMove(const_cast<FMoverTickStartData&>(StartState), DeltaSeconds, SimContext, DesiredVelocity, DesiredFacing, CurrentFacing, OutProposedMove.AngularVelocityDegrees, OutProposedMove.LinearVelocity);

	SimContext.Blackboard.TrySet(DidGenerateMoveEntry, true);
}

void USimpleWalkingMode::GenerateWalkMove_Implementation(FMoverTickStartData& StartState, float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
	InOutVelocity = DesiredVelocity;

	FQuat ToFacing = CurrentFacing.Inverse() * DesiredFacing;
	InOutAngularVelocityDegrees = DeltaSeconds > 0.0f ? FMath::RadiansToDegrees(ToFacing.ToRotationVector() / DeltaSeconds) : FVector::ZeroVector;
}

