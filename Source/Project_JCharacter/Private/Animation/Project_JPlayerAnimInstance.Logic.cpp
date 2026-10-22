#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "AnimationWarpingLibrary.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchHistoryCollectorAnimNodeLibrary.h"
#include "Chooser.h"
#include "StructUtils/InstancedStruct.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/BlueprintSpringMathLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace
{
double Map(double Value,double A,double B,double C,double D)
{
	return FMath::GetMappedRangeValueClamped(FVector2D(A,B),FVector2D(C,D),Value);
}
bool Curve(UAnimSequence* Animation,FName Name,float Time,float& Value)
{
	Value=0.f;
	return UAnimationWarpingLibrary::GetCurveValueFromAnimation(Animation,Name,Time,Value);
}
}

void UProject_JPlayerAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	UpdateNativeLocomotionLogic();
}
void UProject_JPlayerAnimInstance::UpdateNativeLocomotionLogic()
{
	if (!PlayerHasOwningActor || !PlayerHasLocomotionSource || !PlayerUseThreadSafeUpdateAnimation) return;
	if (bNeedsLocomotionHistorySeed) PlayerTrajectoryCirclingTime = 0.;
	UpdateNativeTrajectory();
	if (bNeedsLocomotionHistorySeed) SeedLocomotionHistory();
	UpdateNativeEssentialValues();UpdateNativeStates();UpdateNativeAimOffset();UpdateNativeAdditiveLean();
}

void UProject_JPlayerAnimInstance::SeedLocomotionHistory()
{
	// Seed at the first admitted source frame, including after retained-instance reinit.
	PlayerCharacterTransform = PlayerCharacterTransformLastFrame = PlayerCharacterProperties.ActorTransform;
	PlayerVelocity = PlayerVelocityLastFrame = PlayerLastNonZeroVelocity = PlayerCharacterProperties.Velocity;
	PlayerAcceleration = PlayerAccelerationLastFrame = PlayerCharacterProperties.InputAcceleration;
	PlayerSmoothedGroundNormal = PlayerCharacterProperties.GroundNormal;
	PlayerSmoothedAimTarget = PlayerCharacterProperties.AimingRotation;
	PlayerAimAngularVelocity = FVector::ZeroVector;
	PlayerAO = PlayerPreviousAO = FVector2D::ZeroVector;
	PlayerFutureFacingDeltaLastFrame = PlayerFutureFacingDelta;
	PlayerTrajectoryPreviousFutureVelocity = PlayerTrajectoryFutureVelocity;
	PlayerFutureFacingOnTransitionStart = PlayerTrajectoryFutureFacing;
	auto Seed=[]<typename T>(T& Value,T& Last,T& Recent,double& Time,double& LastTime,T New)
	{ Value=Last=Recent=New; Time=LastTime=0.; };
	Seed(PlayerMovementMode,PlayerMovementModeLastFrame,PlayerMovementModeRecent,PlayerMovementModeTime,PlayerMovementModeLastStateTime,PlayerCharacterProperties.MovementMode);
	Seed(PlayerRotationMode,PlayerRotationModeLastFrame,PlayerRotationModeRecent,PlayerRotationModeTime,PlayerRotationModeLastStateTime,PlayerCharacterProperties.RotationMode);
	Seed(PlayerMovementState,PlayerMovementStateLastFrame,PlayerMovementStateRecent,PlayerMovementStateTime,PlayerMovementStateLastStateTime,EvaluateIsMoving()?EProject_JPlayerMovementState::Moving:EProject_JPlayerMovementState::Idle);
	Seed(PlayerGait,PlayerGaitLastFrame,PlayerGaitRecent,PlayerGaitTime,PlayerGaitLastStateTime,PlayerCharacterProperties.Gait==EProject_JLocomotionGaitIntent::Sprint && PlayerTrajectoryIsCircling?EProject_JLocomotionGaitIntent::Run:PlayerCharacterProperties.Gait);
	Seed(PlayerStance,PlayerStanceLastFrame,PlayerStanceRecent,PlayerStanceTime,PlayerStanceLastStateTime,PlayerCharacterProperties.Stance);
	Seed(PlayerMovementDirection,PlayerMovementDirectionLastFrame,PlayerMovementDirectionRecent,PlayerMovementDirectionTime,PlayerMovementDirectionLastStateTime,PlayerCharacterProperties.MovementDirection);
	bNeedsLocomotionHistorySeed=false;
}
void UProject_JPlayerAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	PlayerForceFootPlacementReset = false;
}

bool UProject_JPlayerAnimInstance::HasBlendStackTag(FName Tag) const
{
 return PlayerBlendStackInputs.Tags.Contains(Tag);
}

bool UProject_JPlayerAnimInstance::EvaluateNativeTransition(EProject_JPlayerTransitionRule Rule,float StateElapsedTime)
{
 // Do not cache this with the frame snapshot: each transition owns its source-state clock,
 // and state-entry selection/notify callbacks may change the blend-stack inputs in this update.
 using RuleType=EProject_JPlayerTransitionRule;
 switch(Rule)
 {
 case RuleType::Moving:return PlayerMovementState==EProject_JPlayerMovementState::Moving;
 case RuleType::Idle:return PlayerMovementState==EProject_JPlayerMovementState::Idle;
 case RuleType::AirTransitionComplete:return IsProjectAirTransitionComplete() && StateElapsedTime>0.f;
 case RuleType::NoValidAnimationOrLooping:return PlayerNoValidAnim || PlayerBlendStackInputs.Loop;
 case RuleType::StanceChanged:return PlayerStance!=PlayerStanceLastFrame && StateElapsedTime>0.f;
 case RuleType::NewPivot:return EvaluateIsPivoting() && StateElapsedTime>0.f && (StateElapsedTime>.5f || !HasBlendStackTag(TEXT("Pivot")));
 case RuleType::LocomotionSettingsChanged:return (PlayerMovementDirection!=PlayerMovementDirectionLastFrame || PlayerStance!=PlayerStanceLastFrame || PlayerGait!=PlayerGaitLastFrame) && StateElapsedTime>0.f;
 case RuleType::TransitionPivot:return EvaluateIsPivoting() && StateElapsedTime>(PlayerBlendStackInputs.Loop?0.f:.5f) && !PlayerTrajectoryIsCircling && PlayerMovementModeRecent==EProject_JPlayerMovementMode::OnGround;
 case RuleType::InAir:return PlayerMovementMode==EProject_JPlayerMovementMode::InAir;
 case RuleType::Grounded:return PlayerMovementMode==EProject_JPlayerMovementMode::OnGround;
 case RuleType::IdleTurnRedirect:
  return EvaluateTurnInPlace() && StateElapsedTime>0.f &&
   ((GetCurveValue(TEXT("Enable_TurnInPlaceSteering"))<.1f && StateElapsedTime>.3f) ||
    (HasBlendStackTag(TEXT("Spin_L")) && PlayerFutureFacingDelta>45.) ||
    (HasBlendStackTag(TEXT("Spin_R")) && PlayerFutureFacingDelta< -45.));
 case RuleType::EarlyFacingRedirect:
  return FMath::Abs((PlayerTrajectoryFutureFacing-PlayerFutureFacingOnTransitionStart).GetNormalized().Yaw)>60. && StateElapsedTime<.5f && StateElapsedTime>0.f && !PlayerBlendStackInputs.Loop && !PlayerTrajectoryIsCircling;
 case RuleType::IdleBreakElapsed:return StateElapsedTime>3.f;
 case RuleType::NotifyRedirect:return PlayerNotifyTransitionReTransition;
 case RuleType::NotifyLoop:return PlayerNotifyTransitionToLoop;
 case RuleType::Circling:return PlayerTrajectoryCirclingTime>.25;
 case RuleType::TrajectoryDiscontinuity:return FMath::Abs(PlayerFutureFacingDelta-PlayerFutureFacingDeltaLastFrame)>200. && StateElapsedTime>0.f;
 case RuleType::MovementDirectionChanged:return PlayerMovementDirection!=PlayerMovementDirectionLastFrame && StateElapsedTime>0.f;
 case RuleType::SlopeChanged:return FMath::Abs(PlayerSlopeAngle.Y)>15. && StateElapsedTime>.75f;
 }
 return false;
}

double UProject_JPlayerAnimInstance::EvaluateTurnInPlaceSteeringAlpha(const FAnimNodeReference& Node)
{
 float Value=0.f;
 Curve(Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node)),TEXT("Enable_TurnInPlaceSteering"),UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node),Value);
 return Value;
}

void UProject_JPlayerAnimInstance::UpdateNativeConsoleVariables()
{
	check(IsInGameThread());
 static IConsoleVariable* Controls[5]={};
 const TCHAR* Names[]={TEXT("a.animnode.offsetrootbone.enable"),TEXT("DDCvar.OffsetRootBone.TranslationRadius"),TEXT("DDCVar.FootPlacementMode"),TEXT("a.AnimNode.FootPlacement.Enable"),TEXT("a.AnimNode.FootPlacement.Debug")};
 for(int32 Index=0;Index<5;++Index)if(!Controls[Index])Controls[Index]=IConsoleManager::Get().FindConsoleVariable(Names[Index]);
 PlayerOffsetRootBoneEnabled=Controls[0] && Controls[0]->GetBool();
 PlayerOffsetRootTranslationRadius=Controls[1]?Controls[1]->GetFloat():0.;
 PlayerFootPlacementMode=Controls[2]?Controls[2]->GetInt():0;
 PlayerFootPlacementEnable=Controls[3] && Controls[3]->GetBool();
 PlayerFootPlacementDebug=Controls[4] && Controls[4]->GetBool();
}

bool UProject_JPlayerAnimInstance::EvaluateIsMoving()
{
	const auto& Trajectory=PlayerTrajectory;
	FVector Future; UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Trajectory,.9f,1.f,Future,true);
	// Preserve the authored reachable return: the role switch in IsMoving was disconnected.
	return !Future.Equals(FVector::ZeroVector,10.);
}
bool UProject_JPlayerAnimInstance::EvaluateIsStarting()
{
	return EvaluateIsMoving() && PlayerTrajectoryFutureVelocity.Size2D()>=PlayerVelocity.Size2D()+100.
		&& PlayerSpeed2D<=100.;
}
bool UProject_JPlayerAnimInstance::EvaluateTurnInPlace()
{
	return FMath::Abs(PlayerFutureFacingDelta)>=50. && PlayerSpeed2D<50. && (PlayerMovementState == EProject_JPlayerMovementState::Idle);
}
bool UProject_JPlayerAnimInstance::EvaluateSpinTransition()
{
	return FMath::Abs(PlayerFutureFacingDelta)>=130. && PlayerSpeed2D>=150.;
}
bool UProject_JPlayerAnimInstance::EvaluateLightLanding()
{
	return (PlayerMovementMode == EProject_JPlayerMovementMode::OnGround) && (PlayerMovementModeLastFrame == EProject_JPlayerMovementMode::InAir)
		&& PlayerVelocityLastFrame.Z>PlayerHeavyLandSpeedThreshold;
}
bool UProject_JPlayerAnimInstance::EvaluateHeavyLanding()
{
	return (PlayerMovementMode == EProject_JPlayerMovementMode::OnGround) && (PlayerMovementModeLastFrame == EProject_JPlayerMovementMode::InAir)
		&& PlayerVelocityLastFrame.Z<=PlayerHeavyLandSpeedThreshold;
}
bool UProject_JPlayerAnimInstance::EvaluateTeleport()
{
	return FVector::DistSquared(PlayerCharacterTransform.GetLocation(),PlayerCharacterTransformLastFrame.GetLocation())>FMath::Square(PlayerTeleportThreshold);
}
bool UProject_JPlayerAnimInstance::EvaluateTraversed()
{
	return !IsSlotActive(TEXT("DefaultSlot")) && GetCurveValue(TEXT("MovingTraversal"))>0.f && FMath::Abs(EvaluateTrajectoryTurnAngle())<=100.;
}
double UProject_JPlayerAnimInstance::EvaluateTrajectoryTurnAngle()
{
	return (PlayerTrajectoryFutureVelocity.Rotation()-PlayerVelocity.Rotation()).GetNormalized().Yaw;
}
double UProject_JPlayerAnimInstance::EvaluateTotalFacingDelta(const TArray<float>& Times)
{
	const auto& Trajectory=PlayerTrajectory;
	double Angle=Times.IsEmpty()?0.:(Trajectory.GetSampleAtTime(Times[0],false).Facing.Rotator()-ReadOffsetRootTransform().Rotator()).GetNormalized().Yaw;
	for (int32 Index=1;Index<Times.Num();++Index)
		Angle+=(Trajectory.GetSampleAtTime(Times[Index],false).Facing.Rotator()-Trajectory.GetSampleAtTime(Times[Index-1],false).Facing.Rotator()).GetNormalized().Yaw;
	return Angle;
}
double UProject_JPlayerAnimInstance::EvaluateLandVelocity() { return PlayerCharacterProperties.LandVelocity.Z; }
FVector UProject_JPlayerAnimInstance::EvaluateBlendSpaceInputs()
{
	const double Pitch=PlayerSlopeAngle.Y;
	return HasBlendStackTag(TEXT("BS_Slope")) && FMath::Abs(Pitch)>=15.?FVector(Map(Pitch,-30.,30.,-1.,1.),0.,0.):FVector::ZeroVector;
}
FVector UProject_JPlayerAnimInstance::EvaluateStrafeWarpDirection()
{
	return FMath::Lerp(PlayerLastNonZeroVelocity,PlayerTrajectoryNearFutureVelocity,Map(FMath::Abs(PlayerTrajectoryCurrentAngularVelocity.Z),20.,100.,0.,1.));
}
bool UProject_JPlayerAnimInstance::EvaluateFootPinning() { return (PlayerMovementMode == EProject_JPlayerMovementMode::OnGround); }
bool UProject_JPlayerAnimInstance::EvaluateSlopeWarping() { return EvaluateFootPinning() || (PlayerMovementMode == EProject_JPlayerMovementMode::Sliding); }
EOffsetRootBoneMode UProject_JPlayerAnimInstance::EvaluateRootRotationMode()
{
	return IsSlotActive(TEXT("DefaultSlot")) || (PlayerMovementMode == EProject_JPlayerMovementMode::Ragdoll) || (PlayerMovementMode == EProject_JPlayerMovementMode::Flying)?EOffsetRootBoneMode::Release:EOffsetRootBoneMode::Accumulate;
}
EOffsetRootBoneMode UProject_JPlayerAnimInstance::EvaluateRootTranslationMode()
{
	return !IsSlotActive(TEXT("DefaultSlot")) && EvaluateFootPinning() && EvaluateIsMoving()?EOffsetRootBoneMode::Interpolate:EOffsetRootBoneMode::Release;
}
double UProject_JPlayerAnimInstance::EvaluateRootTranslationHalfLife() { return (PlayerMovementState == EProject_JPlayerMovementState::Idle)?.1:.3; }
double UProject_JPlayerAnimInstance::EvaluateRootTranslationRadius() { return PlayerOffsetRootTranslationRadius; }
EOrientationWarpingSpace UProject_JPlayerAnimInstance::EvaluateOrientationWarpingSpace()
{
	return PlayerOffsetRootBoneEnabled?EOrientationWarpingSpace::RootBoneTransform:EOrientationWarpingSpace::ComponentTransform;
}
double UProject_JPlayerAnimInstance::EvaluateWarpAlpha(const FAnimNodeReference& Node)
{
	if (auto* Sequence=Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node)))
	{ float Value;Curve(Sequence,TEXT("Enable_Warping"),UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node),Value);return Value; }
	return GetCurveValue(TEXT("Enable_Warping"));
}
double UProject_JPlayerAnimInstance::EvaluateProceduralTargetTime(const FAnimNodeReference& Node)
{
	float Value=0.f;
	Curve(Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node)),TEXT("SteeringTargetTime"),UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node),Value);
	return Map(Value,0.,1.,.1,.3);
}
bool UProject_JPlayerAnimInstance::EvaluateSteering(const FAnimNodeReference& Node)
{
	return ((UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimIsActive(Node) || PlayerBlendStackInputs.Loop) && EvaluateIsMoving())
		|| (PlayerMovementMode == EProject_JPlayerMovementMode::Sliding) || (PlayerMovementMode == EProject_JPlayerMovementMode::InAir);
}

double UProject_JPlayerAnimInstance::EvaluateDynamicPlayRate(const FAnimNodeReference& Node)
{
	// Preserve authored ground cadence; stride warping consumes actual locomotion
	// speed. Keep the existing air curve policy and montage clocks independent.
	const auto& Ground=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground;
	if (PlayerMovementMode==EProject_JPlayerMovementMode::OnGround && !Ground.bScalePlayRateWithSpeed) return 1.;
	auto* Sequence=Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node));
	const float Time=Sequence?UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node):0.f;
	auto ReadCurve=[&](FName Name,float& Value)
	{
		return Sequence?Curve(Sequence,Name,Time,Value):GetCurveValueWithDefault(Name,0.f,Value);
	};
	float Alpha,Speed,Minimum,Maximum;
	if (!ReadCurve(TEXT("Enable_Warping"),Alpha) || !ReadCurve(TEXT("MoveData_Speed"),Speed)) return 1.;
	if (!ReadCurve(TEXT("MinDynamicPlayRate"),Minimum)) Minimum=1.f;
	if (!ReadCurve(TEXT("MaxDynamicPlayRate"),Maximum)) Maximum=1.25f;
	const double Rate=FMath::Clamp(PlayerSpeed2D/FMath::Clamp(double(Speed),.1,999.),double(Minimum),double(Maximum));
	const double Turn=Map(FMath::Abs(PlayerTrajectoryCurrentAngularVelocity.Z),100.,400.,1.,1.2);
	return FMath::Lerp(1.,Rate,double(Alpha))*FMath::Lerp(1.,Turn,Map(PlayerTrajectoryCirclingTime,0.,.5,0.,1.));
}

bool UProject_JPlayerAnimInstance::EvaluateIsPivoting()
{
	if (!(PlayerMovementState == EProject_JPlayerMovementState::Moving)) return false;
	const double Angle=FMath::Abs(PlayerTrajectoryTurnAngle);
	// Production owns the GASP state-controller path. The retired MM setup switch
	// is no longer authored; treating its missing value as zero selected the old rule.
	const int32 Gait=int32(static_cast<double>(PlayerGait));const double Speed=PlayerSpeed2D;
	const double Minimum=Gait==2?200.:Gait==1?175.:50.;
	const auto& Ground=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground;
	// Preserve authored headroom (700-585 / 600-375 / 600-585). Run must
	// also admit carried Sprint momentum after a downshift or landing.
	const double Maximum=Gait==2?double(Ground.SprintSpeed)+115.:
		FMath::Max(double(Ground.RunSpeed)+225.,double(Ground.SprintSpeed)+15.);
	const bool bSpeed=(PlayerStance == EProject_JPlayerStance::Crouch)?Speed>=50. && Speed<=200.:Speed>=Minimum && Speed<=Maximum;
	const double Threshold=Gait==2?60.:
		(PlayerMovementModeRecent == EProject_JPlayerMovementMode::InAir) || (PlayerMovementModeRecent == EProject_JPlayerMovementMode::Sliding)?100.:
		(PlayerMovementModeRecent == EProject_JPlayerMovementMode::Ragdoll) || (PlayerMovementModeRecent == EProject_JPlayerMovementMode::Flying)?0.:75.;
	return bSpeed && Angle>=Threshold;
}

void UProject_JPlayerAnimInstance::UpdateNativeTrajectory()
{
	const FTransformTrajectory Trajectory=GetFoundationTrajectory();PlayerTrajectory = Trajectory;
	auto Velocity=[&](FVector& Value,float A,float B)
	{
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Trajectory,A,B,Value,false);
	};
	Velocity(PlayerTrajectoryPastVelocity,-.3f,-.2f);Velocity(PlayerTrajectoryNearFutureVelocity,.1f,.2f);
	PlayerTrajectoryPreviousFutureVelocity = PlayerTrajectoryFutureVelocity;
	Velocity(PlayerTrajectoryFutureVelocity,.4f,.5f);
	PlayerTrajectoryTurnAngle = EvaluateTrajectoryTurnAngle();
	PlayerTrajectoryFutureFacing = Trajectory.GetSampleAtTime(1.5f,false).Facing.Rotator();
	const double Previous=PlayerFutureFacingDelta;
	PlayerFutureFacingDeltaLastFrame = Previous;
	const double Future=EvaluateTotalFacingDelta({0.,.25,.75,1.5});PlayerFutureFacingDelta = Future;
	if (FMath::Abs(Future-Previous)>200.)
	{
		PlayerMovementDirection = EProject_JPlayerMovementDirection::Backward;PlayerMovementDirectionRecent = EProject_JPlayerMovementDirection::Backward;
	}
	FVector Past,Current;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(Trajectory,-.4f,-.3f,Past,false);
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(Trajectory,0.f,.1f,Current,false);
	PlayerTrajectoryPastAngularVelocity = Past;PlayerTrajectoryCurrentAngularVelocity = Current;
	const bool bCircling=((Past.Z>200. && Current.Z>200.) || (Past.Z<-200. && Current.Z<-200.)) && FMath::Abs(PlayerTrajectoryTurnAngle)>50.;
	PlayerTrajectoryIsCircling = bCircling;
	PlayerTrajectoryCirclingTime = bCircling?PlayerTrajectoryCirclingTime+GetDeltaSeconds():0.;
}

void UProject_JPlayerAnimInstance::UpdateNativeEssentialValues()
{
	const FTransform Character=PlayerCharacterProperties.ActorTransform;
	PlayerCharacterTransformLastFrame = PlayerCharacterTransform;PlayerCharacterTransform = Character;
	FTransform Root=Character;
	if (PlayerOffsetRootBoneEnabled)
	{
		Root=ReadOffsetRootTransform();FRotator Rotation=Root.Rotator();Rotation.Yaw+=90.;Root=FTransform(Rotation,Root.GetLocation());
	}
	PlayerRootTransform = Root;
	PlayerAccelerationLastFrame = PlayerAcceleration;
	const FVector Acceleration=PlayerCharacterProperties.InputAcceleration;PlayerAcceleration = Acceleration;
	const double Maximum=PlayerCharacterProperties.CurrentMaxAcceleration;
	const double Amount=Maximum!=0.?Acceleration.Size()/Maximum:0.;
	PlayerAccelerationAmount = Amount;PlayerHasAcceleration = Amount>0.;
	const FVector Previous=PlayerVelocity;PlayerVelocityLastFrame = Previous;
	const FVector Velocity=PlayerCharacterProperties.Velocity;PlayerVelocity = Velocity;
	const double Speed=Velocity.Size2D();PlayerSpeed2D = Speed;PlayerHasVelocity = Speed>5.;
	const FVector Derivative=(Velocity-Previous)/FMath::Max(double(GetDeltaSeconds()),.001);
	PlayerVelocityAcceleration = Derivative;PlayerRelativeAcceleration = Root.GetRotation().UnrotateVector(Derivative);
	if (Speed>5.) PlayerLastNonZeroVelocity = Velocity;
	const FVector Normal=FMath::VInterpTo(PlayerSmoothedGroundNormal,PlayerCharacterProperties.GroundNormal,GetDeltaSeconds(),5.f);
	PlayerSmoothedGroundNormal = Normal;
	float Pitch,Roll;UKismetMathLibrary::GetSlopeDegreeAngles(Root.GetUnitAxis(EAxis::Y),Normal,Character.GetUnitAxis(EAxis::Z),Pitch,Roll);
	PlayerSlopeAngle = FVector2D(Roll,Pitch);
}

void UProject_JPlayerAnimInstance::UpdateNativeStates()
{
 auto Update=[this]<typename T>(T& Value,T& Last,T& Recent,double& Time,double& LastTime,T New,double Limit)
 {
  const T Previous=Value;Last=Previous;Value=New;
  if(Previous!=New){LastTime=Time;Time=0.;}
  else{Time+=GetDeltaSeconds(); if(Time>=Limit)Recent=New;}
 };
 Update(PlayerMovementMode,PlayerMovementModeLastFrame,PlayerMovementModeRecent,PlayerMovementModeTime,PlayerMovementModeLastStateTime,PlayerCharacterProperties.MovementMode,.2);
 Update(PlayerRotationMode,PlayerRotationModeLastFrame,PlayerRotationModeRecent,PlayerRotationModeTime,PlayerRotationModeLastStateTime,PlayerCharacterProperties.RotationMode,.1);
 Update(PlayerMovementState,PlayerMovementStateLastFrame,PlayerMovementStateRecent,PlayerMovementStateTime,PlayerMovementStateLastStateTime,EvaluateIsMoving()?EProject_JPlayerMovementState::Moving:EProject_JPlayerMovementState::Idle,.1);
 Update(PlayerGait,PlayerGaitLastFrame,PlayerGaitRecent,PlayerGaitTime,PlayerGaitLastStateTime,PlayerCharacterProperties.Gait==EProject_JLocomotionGaitIntent::Sprint && PlayerTrajectoryIsCircling?EProject_JLocomotionGaitIntent::Run:PlayerCharacterProperties.Gait,.1);
 Update(PlayerStance,PlayerStanceLastFrame,PlayerStanceRecent,PlayerStanceTime,PlayerStanceLastStateTime,PlayerCharacterProperties.Stance,.1);
 Update(PlayerMovementDirection,PlayerMovementDirectionLastFrame,PlayerMovementDirectionRecent,PlayerMovementDirectionTime,PlayerMovementDirectionLastStateTime,PlayerCharacterProperties.MovementDirection,.1);

}

FTransform UProject_JPlayerAnimInstance::ReadOffsetRootTransform()
{
	if (const auto* Node=FindFoundationNode(TEXT("OffsetRoot")).GetAnimNodePtr<FAnimNode_OffsetRootBone>())
		return FTransform(Node->GetOffsetRootRotation(),Node->GetOffsetRootTranslation());
	return FTransform::Identity;
}

FAnimNodeReference UProject_JPlayerAnimInstance::FindFoundationNode(FName Tag)
{
	const auto* Interface=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetAnimClassInterface();
	const auto* Tags=Interface?Interface->FindSubsystem<FAnimSubsystem_Tag>():nullptr;
	const int32 Index=Tags?Tags->FindNodeIndexByTag(Tag):INDEX_NONE;
	return Index==INDEX_NONE?FAnimNodeReference():FAnimNodeReference(this,Index);
}
FQuat UProject_JPlayerAnimInstance::EvaluateDesiredFacing(const FAnimNodeReference& Node)
{
	float Value=0.f;
	Curve(Cast<UAnimSequence>(UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAsset(Node)),TEXT("SteeringTargetTime"),UBlendStackAnimNodeLibrary::GetCurrentBlendStackAnimAssetTime(Node),Value);
	return PlayerTrajectory.GetSampleAtTime(Map(Value,0.,1.,.1,1.5),false).Facing;
}
bool UProject_JPlayerAnimInstance::EvaluateAnimationAlmostComplete()
{
	EAnimNodeReferenceConversionResult Result;const auto Node=UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(FindFoundationNode(TEXT("State Machine Blend Stack")),Result);
	return Result==EAnimNodeReferenceConversionResult::Succeeded && !UBlendStackAnimNodeLibrary::IsCurrentAssetLooping(Node) && UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(Node)<=.75f;
}
void UProject_JPlayerAnimInstance::SelectNativeStateAnimation(FName Name,uint8 State,bool)
{
#if WITH_EDITOR
	// Asset/map previews have no simulated locomotion source. Do not run pose
	// matching with empty character state while their async indexes are loading.
	// Runtime state entry retains its existing initialization/retry contract.
	if (!PlayerHasLocomotionSource && GetWorld() && !GetWorld()->IsGameWorld()) return;
#endif
 // Preserve selection at state entry, including per-node pose history.
 PlayerStateName=Name;PlayerStateMachineState=static_cast<EProject_JPlayerPlaybackState>(State);
 PlayerNotifyTransitionReTransition=PlayerNotifyTransitionToLoop=false;
 PlayerPreviousBlendStackInputs=PlayerBlendStackInputs;PlayerNoValidAnim=false;
 FProject_JPlayerGroundChooserOutput Output;
 FChooserEvaluationContext Context;Context.AddObjectParam(this);Context.AddStructParam(Output);
 EAnimNodeReferenceConversionResult Result;
 auto History=UPoseSearchHistoryCollectorAnimNodeLibrary::GetPoseHistoryReference(UPoseSearchHistoryCollectorAnimNodeLibrary::ConvertToPoseHistoryNode(FindFoundationNode(TEXT("PoseHistory")),Result));
 Context.AddStructParam(History);UAnimationAsset* Selected=nullptr;
 if(GroundAnimationChooser)UChooserTable::EvaluateChooser(Context,GroundAnimationChooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
 {Selected=Cast<UAnimationAsset>(Object);return Selected?FObjectChooserBase::EIteratorStatus::Stop:FObjectChooserBase::EIteratorStatus::Failed;}));
 if(!Selected){PlayerNoValidAnim=true;return;}
 bool Loop=false;UPoseSearchLibrary::IsAnimationAssetLooping(Selected,Loop);
 PlayerBlendStackInputs.Anim=Selected;PlayerBlendStackInputs.Loop=Loop;
 PlayerBlendStackInputs.StartTime=Output.StartTime;PlayerBlendStackInputs.BlendTime=Output.BlendTime;
 PlayerBlendStackInputs.BlendCurve=Output.BlendCurve;PlayerBlendStackInputs.Tags=MoveTemp(Output.Tags);
 PlayerBlendStackInputs.BlendProfile=const_cast<UBlendProfile*>(GetBlendProfileByName(Output.BlendProfile));
	if (!Loop)
	{
		const auto Stack=UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(FindFoundationNode(TEXT("State Machine Blend Stack")),Result);
		if (Result==EAnimNodeReferenceConversionResult::Succeeded) UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Stack);
	}
}
void UProject_JPlayerAnimInstance::EnterNativeLocomotionTransition()
{
	PlayerFutureFacingOnTransitionStart = PlayerTrajectoryFutureFacing;
 SelectNativeStateAnimation(TEXT("Locomotion Transition"),uint8(EProject_JPlayerPlaybackState::LocomotionTransition),true);
}
void UProject_JPlayerAnimInstance::EnterNativeAirTransition()
{
	if (UsesProjectJumpAir()) BeginProjectAir(false);
	else SelectNativeStateAnimation(TEXT("In Air Transition"),uint8(EProject_JPlayerPlaybackState::AirTransition),true);
}
void UProject_JPlayerAnimInstance::EnterNativeAirLoop()
{
	if (UsesProjectJumpAir()) BeginProjectAir(true);
	else SelectNativeStateAnimation(TEXT("In Air Loop"),uint8(EProject_JPlayerPlaybackState::AirLoop),true);
}

void UProject_JPlayerAnimInstance::UpdateNativeAimOffset()
{
	const FRotator Target=PlayerCharacterProperties.AimingRotation;
	FRotator Smoothed=PlayerSmoothedAimTarget;FVector AngularVelocity=PlayerAimAngularVelocity;
	UBlueprintSpringMathLibrary::CriticalSpringDampRotator(Smoothed,AngularVelocity,Target,GetDeltaSeconds(),.2f);
	const FVector2D Previous=PlayerAO;PlayerPreviousAO = Previous;
	const FRotator Root=PlayerRootTransform.Rotator();const FRotator Delta=(Smoothed-Root).GetNormalized();
	const FVector2D AO(Delta.Yaw,Delta.Pitch);PlayerAO = AO;
	const bool Enabled=FMath::Abs((Target-Root).GetNormalized().Yaw)<=((PlayerMovementState == EProject_JPlayerMovementState::Idle)?110.:180.)
		&& ((PlayerRotationMode == EProject_JPlayerRotationMode::Strafe) || (PlayerRotationMode == EProject_JPlayerRotationMode::Aim))
		&& Blueprint_GetSlotMontageLocalWeight(TEXT("DefaultSlot"))<.5f && FMath::Abs(AO.X-Previous.X)<135.;
	PlayerEnableAO = Enabled;
	PlayerSmoothedAimTarget = Enabled?Smoothed:Target;PlayerAimAngularVelocity = Enabled?AngularVelocity:FVector::ZeroVector;
}
void UProject_JPlayerAnimInstance::UpdateNativeAdditiveLean()
{
	const FVector Local=PlayerVelocity.Rotation().UnrotateVector(PlayerVelocityAcceleration);
	const double Amount=FMath::Clamp(Local.Y/Map(PlayerSpeed2D,200.,320.,500.,800.),-1.,1.);
	const FVector2D Lean=(PlayerMovementDirection == EProject_JPlayerMovementDirection::Forward)?FVector2D(Amount,0.):
		(PlayerMovementDirection == EProject_JPlayerMovementDirection::Backward)?FVector2D(-Amount,0.):
		(PlayerMovementDirection == EProject_JPlayerMovementDirection::LeftLeftFootForward) || (PlayerMovementDirection == EProject_JPlayerMovementDirection::LeftRightFootForward)?FVector2D(0.,Amount):FVector2D(0.,-Amount);
	PlayerLeanAmount = Lean;
}
void UProject_JPlayerAnimInstance::UpdateNativeLocomotionTransition()
{
	PlayerFutureFacingOnTransitionStart = FMath::RInterpTo(PlayerFutureFacingOnTransitionStart,PlayerTrajectoryFutureFacing,GetDeltaSeconds(),4.f);
}

void UProject_JPlayerAnimInstance::UpdateNativeRootOffset(const FAnimNodeReference& Node)
{
	if (auto* Offset=Node.GetAnimNodePtr<FAnimNode_OffsetRootBone>())
	{
		const FTransform Delta=PlayerCharacterProperties.BasedMovementDelta;
		Offset->SetOffsetRootTranslation(Offset->GetOffsetRootTranslation()+Delta.GetTranslation());
		Offset->SetOffsetRootRotation(Delta.GetRotation()*Offset->GetOffsetRootRotation());
	}
}
void UProject_JPlayerAnimInstance::ResetNativeFootPlacement()
{
	PlayerForceFootPlacementReset = true;
}

void UProject_JPlayerAnimInstance::RequestLocomotionTransition(bool bToLoop, bool bRequireGaitChange,
 EProject_JLocomotionGaitIntent ExcludedGait)
{
 check(IsInGameThread());
 if (bRequireGaitChange && int32(static_cast<double>(PlayerGait)) == int32(ExcludedGait)) return;
 if(bToLoop)PlayerNotifyTransitionToLoop=true;else PlayerNotifyTransitionReTransition=true;
}
