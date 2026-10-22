#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MoverSimulationTypes.h"
#include "Project_JGroundMovement.generated.h"

class USmoothWalkingMode;

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

/** Thin BP adapter required by the installed engine's non-exported SmoothWalking constructors. */
UCLASS()
class PROJECT_JCORE_API UProject_JGroundMovementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Strafe sprint supports forward and the two forward-diagonal input sectors. */
	static bool SupportsStrafeSprint(const FVector& MoveDirection,const FVector& Facing);
	/** Preserve the authored normalized-angle convention, including the exact 180 degree tie. */
	static FQuat ResolveGroundFacing(const FQuat& CurrentFacing,const FQuat& DesiredFacing,float RotationOffset,float& CurrentOffset);
	UFUNCTION(BlueprintCallable, Category="Project J|Movement")
	static void GenerateGroundMove(USmoothWalkingMode* Mode, UPARAM(ref) FMoverTickStartData& StartState,
		float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
		const FQuat& DesiredFacing, const FQuat& CurrentFacing,
		UPARAM(ref) FVector& InOutAngularVelocityDegrees, UPARAM(ref) FVector& InOutVelocity);
};
