#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MoverSimulationTypes.h"
#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "Project_JGroundMovement.generated.h"

class USmoothWalkingMode;
class UProject_JMoverComponent;
struct FTrajectorySampleInfo;
struct FProject_JGroundTrajectoryPrediction;

/** Authored movement tuning; the solver and admission rules remain native. */
USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JGroundMovementSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Speed", meta=(ClampMin="0")) float WalkSpeed=165.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Speed", meta=(ClampMin="0")) float RunSpeed=500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Speed", meta=(ClampMin="0")) float SprintSpeed=750.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Acceleration", meta=(ClampMin="0")) float WalkAcceleration=500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Acceleration", meta=(ClampMin="0")) float RunAcceleration=800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Acceleration", meta=(ClampMin="0")) float SprintAcceleration=300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Acceleration", meta=(ClampMin="0")) float GaitChangeDeceleration=300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Acceleration", meta=(ClampMin="0")) float StoppingDeceleration=1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0")) float RunTurnStrength=8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0")) float SprintTurnStrength=4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0")) float RunFacingTime=.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0")) float SprintFacingTime=.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0")) float IdleFacingTime=.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground") bool bSlopeRelativeVelocity=true;
};

/** Owned native inputs for one project ground preparation step. No live mode, collection or owner. */
struct FProject_JGroundMoveParams
{
	FProject_JGroundMovementSettings Tuning;
	EMoveInputType InputType = EMoveInputType::None;
	bool bHasInput = false;
	bool bIdle = true;
	bool bSprint = false;
	bool bWalk = false;
	bool bHasLandingBraking = false;
	bool bHasGameplayRootMotion = false;
	FVector FloorNormal = FVector::ZeroVector;
	TOptional<float> PolicyMaxSpeed;
	bool bStrafe = false;
	float RotationOffset = 0.f;
	float ControlRotationRate = 0.f;
};

/** Per-step values; tuning/speed fields are intentionally unused for a velocity bypass. */
struct FProject_JPreparedGroundMove
{
	FVector Velocity = FVector::ZeroVector;
	FQuat Facing = FQuat::Identity;
	float CurrentOffset = 0.f;
	float MaxSpeedOverride = 0.f;
	float Acceleration = 0.f;
	float Deceleration = 0.f;
	float TurningStrength = 0.f;
	float FacingSmoothingTime = 0.f;
	float ControlRotationRate = 0.f;
	bool bVelocityBypass = false;
};

/** Shared solver used directly by native walking and available to authored Blueprint extensions. */
UCLASS()
class PROJECT_JCORE_API UProject_JGroundMovementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Strafe sprint supports forward and the two forward-diagonal input sectors. */
	static bool SupportsStrafeSprint(const FVector& MoveDirection,const FVector& Facing);
	/** Preserve the authored normalized-angle convention, including the exact 180 degree tie. */
	static FQuat ResolveGroundFacing(const FQuat& CurrentFacing,const FQuat& DesiredFacing,float RotationOffset,float& CurrentOffset);
	/** Shared pure value preparation used by the live mode and native prediction. */
	static FProject_JPreparedGroundMove PrepareGroundMove(const FProject_JGroundMoveParams& Params,
		const FVector& DesiredVelocity, const FQuat& DesiredFacing, const FQuat& CurrentFacing, const FVector& CurrentVelocity);
	/** Apply the original post-solver turn bound; velocity bypasses have no such bound. */
	static void ApplyGroundRotationLimit(const FProject_JPreparedGroundMove& Prepared, FVector& InOutAngularVelocityDegrees);
	UFUNCTION(BlueprintCallable, Category="Project J|Movement")
	static void GenerateGroundMove(USmoothWalkingMode* Mode, UPARAM(ref) FMoverTickStartData& StartState,
		float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
		const FQuat& DesiredFacing, const FQuat& CurrentFacing,
		UPARAM(ref) FVector& InOutAngularVelocityDegrees, UPARAM(ref) FVector& InOutVelocity);
};

/** Native implementation of the project's former three-node walking adapter.
 * All authored settings remain inherited; Blueprint descendants can still override GenerateWalkMove. */
UCLASS(BlueprintType, Blueprintable)
class PROJECT_JCORE_API UProject_JGroundWalkingMode : public USmoothWalkingMode
{
    GENERATED_BODY()
    friend class UProject_JGroundMovementLibrary;
public:
    /** GT-only admitted native query, inside the Mover prediction-blackboard lifetime.
     * Rejected queries leave output and live tuning untouched for ordinary BP/custom prediction. */
    bool TryPredictTrajectory(UProject_JMoverComponent& Mover, const FMoverSimContext& SimContext,
        const FMoverTickStartData& StartState, const FMoverTimeStep& FirstStep,
        const FMoverPredictTrajectoryParams& PredictionParams, TArray<FTrajectorySampleInfo>& OutSamples);
    virtual void GenerateWalkMove_Implementation(FMoverTickStartData& StartState, float DeltaSeconds, const FMoverSimContext& SimContext,
        const FVector& DesiredVelocity, const FQuat& DesiredFacing, const FQuat& CurrentFacing,
        FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity) override;
private:
    friend class UProject_JMoverComponent;
    bool TryCaptureTrajectory(UProject_JMoverComponent& Mover, const FMoverSimContext& SimContext,
        const FMoverTickStartData& StartState, const FMoverTimeStep& FirstStep,
        const FMoverPredictTrajectoryParams& Query, FProject_JGroundTrajectoryPrediction& OutPrediction) const;
};
