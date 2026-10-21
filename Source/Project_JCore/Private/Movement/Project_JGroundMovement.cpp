#include "Movement/Project_JGroundMovement.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/Modes/SmoothWalkingMode.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "UObject/UnrealType.h"

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

void UProject_JGroundMovementLibrary::GenerateGroundMove(USmoothWalkingMode* Mode, FMoverTickStartData& StartState,
	float DeltaSeconds, const FMoverSimContext& SimContext, const FVector& DesiredVelocity,
	const FQuat& DesiredFacing, const FQuat& CurrentFacing, FVector& InOutAngularVelocityDegrees, FVector& InOutVelocity)
{
	if (!ensure(Mode)) return;
	const auto* Mover=Cast<UProject_JMoverComponent>(Mode->GetOuter());
	if (!ensure(Mover)) return;
	const auto& Tuning=Mover->GroundMovementSettings;
	const auto* Intent=StartState.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
	const auto* Input=StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const float Speed=InOutVelocity.Size2D();
	const bool bIdle=!Input || Input->GetMoveInput().IsNearlyZero();
	const uint8 Gait=Intent?(Intent->bSprint?2:Intent->bWalk?0:1):1;
	// Cache only the engine-owned schema. No per-pawn mutable global state.
	static const FFloatProperty* Acceleration=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("Acceleration"));
	static const FFloatProperty* Deceleration=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("Deceleration"));
	static const FFloatProperty* Turning=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("TurningStrength"));
	static const FFloatProperty* FacingTime=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),TEXT("FacingSmoothingTime"));
	const float RequestedSpeed=Gait==2?Tuning.SprintSpeed:Gait==0?Tuning.WalkSpeed:Tuning.RunSpeed;
	const auto* Policy=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
	Mode->MaxSpeedOverride=Policy?FMath::Min(RequestedSpeed,Policy->MaxSpeed):RequestedSpeed;
	SetSmoothSetting(Mode,Acceleration,Gait==0?Tuning.WalkAcceleration:
		Gait==2 && Speed>Tuning.RunSpeed?Tuning.SprintAcceleration:Tuning.RunAcceleration);
	const auto* Life=StartState.SyncState.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
	SetSmoothSetting(Mode,Deceleration,bIdle?(Life && Life->LandingBrakingTimeMs>0.f?20000.f:Tuning.StoppingDeceleration):Tuning.GaitChangeDeceleration);
	SetSmoothSetting(Mode,Turning,SpeedBlend(Speed,Tuning,Tuning.RunTurnStrength,Tuning.SprintTurnStrength));
	const float FacingError=FMath::FindDeltaAngleDegrees(CurrentFacing.Rotator().Yaw,DesiredFacing.Rotator().Yaw);
	const float MovingFacing=SpeedBlend(Speed,Tuning,Tuning.RunFacingTime,Tuning.SprintFacingTime);
	SetSmoothSetting(Mode,FacingTime,bIdle?Tuning.IdleFacingTime:
		FMath::GetMappedRangeValueClamped(FVector2D(90.f,135.f),FVector2D(MovingFacing,.2f),FMath::Abs(FacingError)));
	const float RotationOffset=Intent && Intent->bStrafe && FMath::IsFinite(Intent->RotationOffset)?FMath::Clamp(Intent->RotationOffset,-180.f,180.f):0.f;
	float CurrentOffset;
	const FQuat Facing=ResolveGroundFacing(CurrentFacing,DesiredFacing,RotationOffset,CurrentOffset);
	FVector Velocity=DesiredVelocity;
	if (Mover->HasGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("Mover.AnimRootMotion"),false),false)) Velocity=InOutVelocity;
	else if (Tuning.bSlopeRelativeVelocity)
	{
		FHitResult Floor;
		Mover->TryGetFloorCheckHitResult(Floor);
		Velocity=FQuat::FindBetweenNormals(FVector::UpVector,Floor.Normal).RotateVector(Velocity);
	}
	// Keep the engine's rollback-owned spring state and simulation implementation.
	Mode->USmoothWalkingMode::GenerateWalkMove_Implementation(StartState,DeltaSeconds,SimContext,Velocity,Facing,CurrentFacing,InOutAngularVelocityDegrees,InOutVelocity);
	const float ControlRate=Intent && FMath::IsFinite(Intent->ControlRotationRate)?Intent->ControlRotationRate:0.f;
	if (CurrentOffset>=135.f) InOutAngularVelocityDegrees.Z=FMath::Clamp(InOutAngularVelocityDegrees.Z,-1000.,double(ControlRate));
	else if (CurrentOffset<=-135.f) InOutAngularVelocityDegrees.Z=FMath::Clamp(InOutAngularVelocityDegrees.Z,double(ControlRate),1000.);
}
