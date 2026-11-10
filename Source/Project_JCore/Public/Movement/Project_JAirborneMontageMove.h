#pragma once

#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "Project_JAirborneMontageMove.generated.h"

/** A bounded montage root-motion window that hands movement back on landing. */
USTRUCT()
struct PROJECT_JCORE_API FProject_JAirborneMontageMove : public FLayeredMove_AnimRootMotion
{
	GENERATED_BODY()

	bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
		const UMoverComponent* Mover, UMoverBlackboard* Blackboard, FProposedMove& OutMove) override;
	FLayeredMoveBase* Clone() const override;
	UScriptStruct* GetScriptStruct() const override;
};
