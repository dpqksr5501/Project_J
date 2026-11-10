#pragma once

#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "Project_JMontageDashMove.generated.h"

/** A grounded montage dash with independent travel direction and a locomotion handoff. */
USTRUCT()
struct PROJECT_JCORE_API FProject_JMontageDashMove : public FLayeredMove_AnimRootMotion
{
    GENERATED_BODY()

    FProject_JMontageDashMove();

    UPROPERTY()
    FVector TravelDirection = FVector::ForwardVector;

    bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
        const UMoverComponent* Mover, UMoverBlackboard* Blackboard, FProposedMove& OutMove) override;
    FLayeredMoveBase* Clone() const override;
    void NetSerialize(FArchive& Ar) override;
    UScriptStruct* GetScriptStruct() const override;
};

template<>
struct TStructOpsTypeTraits<FProject_JMontageDashMove> : TStructOpsTypeTraitsBase2<FProject_JMontageDashMove>
{
    enum { WithCopy = true };
};
