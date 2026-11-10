#include "Movement/Project_JAirborneMontageMove.h"

#include "MoverSimulationTypes.h"
#include "MoverTypes.h"

bool FProject_JAirborneMontageMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
	const UMoverComponent* Mover, UMoverBlackboard* Blackboard, FProposedMove& OutMove)
{
	const double RemainingMs = DurationMs - (TimeStep.BaseSimTimeMs - StartSimTimeMs);
	if (StartState.SyncState.MovementMode != DefaultModeNames::Falling || RemainingMs <= 0.0)
	{
		DurationMs = 0.f;
		return false;
	}
	if (TimeStep.StepMs <= UE_KINDA_SMALL_NUMBER) { return false; }

	// Preserve the authored displacement when a simulation step straddles the end of the window.
	FMoverTimeStep ExtractionStep = TimeStep;
	ExtractionStep.StepMs = FMath::Min<double>(TimeStep.StepMs, RemainingMs);
	if (!FLayeredMove_AnimRootMotion::GenerateMove(StartState, ExtractionStep, Mover, Blackboard, OutMove))
	{
		return false;
	}
	const float Fraction = ExtractionStep.StepMs / TimeStep.StepMs;
	OutMove.LinearVelocity *= Fraction;
	OutMove.AngularVelocityDegrees *= Fraction;
	return true;
}

FLayeredMoveBase* FProject_JAirborneMontageMove::Clone() const
{
	return new FProject_JAirborneMontageMove(*this);
}

UScriptStruct* FProject_JAirborneMontageMove::GetScriptStruct() const
{
	return StaticStruct();
}
