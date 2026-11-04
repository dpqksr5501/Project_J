#include "Movement/Project_JGroundMovement.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "UObject/UnrealType.h"
#include "MoverTypes.h"

namespace
{
void SetSmoothSetting(USmoothWalkingMode* Mode, const FFloatProperty* Property, float Value)
{
	check(Property);
	Property->SetPropertyValue_InContainer(Mode, Value);
}
float SpeedBlend(float Speed, const FProject_JGroundMovementSettings& Tuning, float A, float B)
{
	return FMath::GetMappedRangeValueClamped(FVector2D(Tuning.RunSpeed,Tuning.SprintSpeed),FVector2D(A,B),Speed);
}
}

bool UProject_JGroundMovementLibrary::SupportsStrafeSprint(const FVector& MoveDirection,const FVector& Facing)
{
	return FVector::DotProduct(MoveDirection.GetSafeNormal2D(),Facing.GetSafeNormal2D())>.5f;
}

FQuat UProject_JGroundMovementLibrary::ResolveGroundFacing(const FQuat& CurrentFacing,const FQuat& DesiredFacing,float RotationOffset,float& CurrentOffset)
{
	CurrentOffset=(CurrentFacing.Rotator()-DesiredFacing.Rotator()).GetNormalized().Yaw;
	const float SafeOffset=FMath::Clamp(RotationOffset,CurrentOffset-179.f,CurrentOffset+179.f);
	return (FRotator(0.,SafeOffset,0.).Quaternion()*DesiredFacing).GetNormalized();
}

FProject_JPreparedGroundMove UProject_JGroundMovementLibrary::PrepareGroundMove(const FProject_JGroundMoveParams& Params,
	const FVector& DesiredVelocity, const FQuat& DesiredFacing, const FQuat& CurrentFacing, const FVector& CurrentVelocity)
{
	const auto& Tuning=Params.Tuning;
	FProject_JPreparedGroundMove Prepared;
	Prepared.Velocity=DesiredVelocity;
	Prepared.Facing=DesiredFacing;
	if (Params.bHasGameplayRootMotion) Prepared.Velocity=CurrentVelocity;
	else if (Tuning.bSlopeRelativeVelocity)
	{
		Prepared.Velocity=FQuat::FindBetweenNormals(FVector::UpVector,Params.FloorNormal).RotateVector(Prepared.Velocity);
	}
	Prepared.bVelocityBypass=Params.bHasInput && Params.InputType==EMoveInputType::Velocity;
	if (Prepared.bVelocityBypass) return Prepared;
	const float Speed=CurrentVelocity.Size2D();
	const bool bIdle=Params.bIdle;
	const uint8 Gait=Params.bSprint?2:Params.bWalk?0:1;
	const float RequestedSpeed=Gait==2?Tuning.SprintSpeed:Gait==0?Tuning.WalkSpeed:Tuning.RunSpeed;
	Prepared.MaxSpeedOverride=Params.PolicyMaxSpeed.IsSet()?FMath::Min(RequestedSpeed,Params.PolicyMaxSpeed.GetValue()):RequestedSpeed;
	Prepared.Acceleration=Gait==0?Tuning.WalkAcceleration:
		Gait==2 && Speed>Tuning.RunSpeed?Tuning.SprintAcceleration:Tuning.RunAcceleration;
	Prepared.Deceleration=bIdle?(Params.bHasLandingBraking?20000.f:Tuning.StoppingDeceleration):Tuning.GaitChangeDeceleration;
	Prepared.TurningStrength=SpeedBlend(Speed,Tuning,Tuning.RunTurnStrength,Tuning.SprintTurnStrength);
	const float FacingError=FMath::FindDeltaAngleDegrees(CurrentFacing.Rotator().Yaw,DesiredFacing.Rotator().Yaw);
	const float MovingFacing=SpeedBlend(Speed,Tuning,Tuning.RunFacingTime,Tuning.SprintFacingTime);
	Prepared.FacingSmoothingTime=bIdle?Tuning.IdleFacingTime:
		FMath::GetMappedRangeValueClamped(FVector2D(90.f,135.f),FVector2D(MovingFacing,.2f),FMath::Abs(FacingError));
	const float RotationOffset=Params.bStrafe && FMath::IsFinite(Params.RotationOffset)?FMath::Clamp(Params.RotationOffset,-180.f,180.f):0.f;
	Prepared.Facing=ResolveGroundFacing(CurrentFacing,DesiredFacing,RotationOffset,Prepared.CurrentOffset);
	Prepared.ControlRotationRate=FMath::IsFinite(Params.ControlRotationRate)?Params.ControlRotationRate:0.f;
	return Prepared;
}

void UProject_JGroundMovementLibrary::ApplyGroundRotationLimit(const FProject_JPreparedGroundMove& Prepared, FVector& InOutAngularVelocityDegrees)
{
	if (Prepared.bVelocityBypass) return;
	const float CurrentOffset=Prepared.CurrentOffset;
	const float ControlRate=Prepared.ControlRotationRate;
	if (CurrentOffset>=135.f) InOutAngularVelocityDegrees.Z=FMath::Clamp(InOutAngularVelocityDegrees.Z,-1000.,double(ControlRate));
	else if (CurrentOffset<=-135.f) InOutAngularVelocityDegrees.Z=FMath::Clamp(InOutAngularVelocityDegrees.Z,double(ControlRate),1000.);
}

void UProject_JGroundMovementLibrary::GenerateGroundMove(USmoothWalkingMode* Mode, FMoverTickStartData& StartState,
	float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
	if (!ensure(Mode)) return;
	const auto* Mover=Cast<UProject_JMoverComponent>(Mode->GetOuter());
	if (!ensure(Mover)) return;
	const auto* Intent=StartState.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
	const auto* Input=StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	FProject_JGroundMoveParams Params;
	Params.Tuning=Mover->GroundMovementSettings;
	Params.bHasInput=Input!=nullptr;
	Params.InputType=Input?Input->GetMoveInputType():EMoveInputType::None;
	Params.bHasGameplayRootMotion=Mover->HasGameplayTag(Mover_AnimRootMotion,false);
	if (!Params.bHasGameplayRootMotion && Params.Tuning.bSlopeRelativeVelocity)
	{
		FHitResult Floor;
		Mover->TryGetFloorCheckHitResult(Floor);
		Params.FloorNormal=Floor.Normal;
	}
	if (!(Params.bHasInput && Params.InputType==EMoveInputType::Velocity))
	{
		Params.bIdle=!Input || Input->GetMoveInput().IsNearlyZero();
		Params.bSprint=Intent && Intent->bSprint;
		Params.bWalk=Intent && Intent->bWalk;
		if (const auto* Policy=Mover->FindSharedSettings<UCommonLegacyMovementSettings>()) Params.PolicyMaxSpeed=Policy->MaxSpeed;
		const auto* Life=StartState.SyncState.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
		Params.bHasLandingBraking=Life && Life->LandingBrakingTimeMs>0.f;
		Params.bStrafe=Intent && Intent->bStrafe;
		Params.RotationOffset=Intent?Intent->RotationOffset:0.f;
		Params.ControlRotationRate=Intent?Intent->ControlRotationRate:0.f;
	}
	const auto Prepared=PrepareGroundMove(Params,DesiredVelocity,DesiredFacing,CurrentFacing,InOutVelocity);
	// NavMover's velocity command owns arrival/stopping and bypasses player spring/tuning/turn limits.
	if (Prepared.bVelocityBypass)
	{
		Mode->USimpleWalkingMode::GenerateWalkMove_Implementation(StartState,DeltaSeconds,SimContext,Prepared.Velocity,
			DesiredFacing,CurrentFacing,InOutAngularVelocityDegrees,InOutVelocity);
		return;
	}
	Mode->MaxSpeedOverride=Prepared.MaxSpeedOverride;
	if (auto* NativeMode=Cast<UProject_JGroundWalkingMode>(Mode))
	{
		NativeMode->Acceleration=Prepared.Acceleration;
		NativeMode->Deceleration=Prepared.Deceleration;
		NativeMode->TurningStrength=Prepared.TurningStrength;
		NativeMode->FacingSmoothingTime=Prepared.FacingSmoothingTime;
	}
	else
	{
		// Preserve generic SmoothWalking callers and their original schema checks.
		static const FFloatProperty* Acceleration=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("Acceleration"));
		static const FFloatProperty* Deceleration=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("Deceleration"));
		static const FFloatProperty* Turning=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("TurningStrength"));
		static const FFloatProperty* FacingTime=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("FacingSmoothingTime"));
		SetSmoothSetting(Mode,Acceleration,Prepared.Acceleration);
		SetSmoothSetting(Mode,Deceleration,Prepared.Deceleration);
		SetSmoothSetting(Mode,Turning,Prepared.TurningStrength);
		SetSmoothSetting(Mode,FacingTime,Prepared.FacingSmoothingTime);
	}
	// Keep the engine's rollback-owned spring state and simulation implementation.
	Mode->USmoothWalkingMode::GenerateWalkMove_Implementation(StartState,DeltaSeconds,SimContext,Prepared.Velocity,Prepared.Facing,CurrentFacing,InOutAngularVelocityDegrees,InOutVelocity);
	ApplyGroundRotationLimit(Prepared,InOutAngularVelocityDegrees);
}

void UProject_JGroundWalkingMode::GenerateWalkMove_Implementation(FMoverTickStartData& StartState, float DeltaSeconds,
    const FMoverSimContext& SimContext, const FVector& DesiredVelocity, const FQuat& DesiredFacing, const FQuat& CurrentFacing,
    FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
    UProject_JGroundMovementLibrary::GenerateGroundMove(this, StartState, DeltaSeconds, SimContext, DesiredVelocity,
        DesiredFacing, CurrentFacing, InOutAngularVelocityDegrees, InOutVelocity);
}
