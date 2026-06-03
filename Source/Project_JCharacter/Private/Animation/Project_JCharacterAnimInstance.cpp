// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "Animation/AnimInstanceProxy.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JBaseCharacter.h"
#include "StructUtils/InstancedStruct.h"

// SyncLegacyFieldsFromStructuredData() removed.
// All code now uses sub-struct paths (e.g., Data.Movement.GroundSpeed) directly.

struct FProject_JCharacterAnimInstanceProxy : public FAnimInstanceProxy
{
	FProject_JCharacterAnimInstanceProxy()
	{
		LinkNativeGraph();
	}

	explicit FProject_JCharacterAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
		LinkNativeGraph();
	}

	void QueueGameThreadData(
		const FProject_JAnimThreadSafeData& InData,
		UPoseSearchDatabase* InSelectedDatabase,
		bool bInMotionMatchingEnabled,
		bool bInUpdateMotionMatchingThisFrame);
	const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
	UPoseSearchDatabase* GetCurrentActiveDatabase() const { return CurrentActiveDatabase.Get(); }

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InLayerName) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void LinkNativeGraph();
	void ApplySelectedDatabaseToNativeNode();

	FProject_JAnimThreadSafeData PendingGameThreadData;
	FProject_JAnimThreadSafeData ThreadSafeData;
	bool bMotionMatchingEnabled = true;
	bool bUpdateMotionMatchingThisFrame = true;

	TObjectPtr<UPoseSearchDatabase> CurrentActiveDatabase = nullptr;

	FAnimNode_PoseSearchHistoryCollector NativePoseHistoryNode;
	FAnimNode_MotionMatching NativeMotionMatchingNode;
};

void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(
	const FProject_JAnimThreadSafeData& InData,
	UPoseSearchDatabase* InSelectedDatabase,
	bool bInMotionMatchingEnabled,
	bool bInUpdateMotionMatchingThisFrame)
{
	PendingGameThreadData = InData;
	CurrentActiveDatabase = InSelectedDatabase;
	bMotionMatchingEnabled = bInMotionMatchingEnabled;
	bUpdateMotionMatchingThisFrame = bInUpdateMotionMatchingThisFrame;
}

void FProject_JCharacterAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	ThreadSafeData = PendingGameThreadData;
	ThreadSafeData.DeltaTime = DeltaSeconds;
}

void FProject_JCharacterAnimInstanceProxy::UpdateAnimationNode_WithRoot(
	const FAnimationUpdateContext& InContext,
	FAnimNode_Base* InRootNode,
	FName InLayerName)
{
	if (bUpdateMotionMatchingThisFrame)
	{
		ApplySelectedDatabaseToNativeNode();
		NativePoseHistoryNode.TransformTrajectory = ThreadSafeData.Movement.Trajectory;
	}

	FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
}

FAnimNode_Base* FProject_JCharacterAnimInstanceProxy::GetCustomRootNode()
{
	LinkNativeGraph();
	return &NativePoseHistoryNode;
}

void FProject_JCharacterAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	LinkNativeGraph();
	OutNodes.Add(&NativePoseHistoryNode);
	OutNodes.Add(&NativeMotionMatchingNode);
}

void FProject_JCharacterAnimInstanceProxy::LinkNativeGraph()
{
	NativePoseHistoryNode.Source.SetLinkNode(&NativeMotionMatchingNode);
	NativePoseHistoryNode.bGenerateTrajectory = false;
	NativePoseHistoryNode.PoseCount = 10;
	NativePoseHistoryNode.SamplingInterval = 0.04f;
	NativePoseHistoryNode.TrajectorySpeedMultiplier = 1.0f;
}

void FProject_JCharacterAnimInstanceProxy::ApplySelectedDatabaseToNativeNode()
{
	if (!bMotionMatchingEnabled || !CurrentActiveDatabase)
	{
		NativeMotionMatchingNode.ResetDatabasesToSearch(EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		return;
	}

	NativeMotionMatchingNode.SetDatabaseToSearch(
		CurrentActiveDatabase.Get(),
		EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
}

namespace
{
const TCHAR* ToDebugString(EProject_JWeaponAnimStance WeaponStance)
{
	switch (WeaponStance)
	{
	case EProject_JWeaponAnimStance::None:
		return TEXT("None");
	case EProject_JWeaponAnimStance::OneHanded:
		return TEXT("OneHanded");
	case EProject_JWeaponAnimStance::TwoHanded:
		return TEXT("TwoHanded");
	case EProject_JWeaponAnimStance::DualWield:
		return TEXT("DualWield");
	case EProject_JWeaponAnimStance::Staff:
		return TEXT("Staff");
	case EProject_JWeaponAnimStance::Bow:
		return TEXT("Bow");
	case EProject_JWeaponAnimStance::Unarmed:
		return TEXT("Unarmed");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionGaitIntent GaitIntent)
{
	switch (GaitIntent)
	{
	case EProject_JLocomotionGaitIntent::Walk:
		return TEXT("Walk");
	case EProject_JLocomotionGaitIntent::Run:
		return TEXT("Run");
	case EProject_JLocomotionGaitIntent::Sprint:
		return TEXT("Sprint");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionRotationMode RotationMode)
{
	switch (RotationMode)
	{
	case EProject_JLocomotionRotationMode::OrientToMovement:
		return TEXT("Orient");
	case EProject_JLocomotionRotationMode::Strafe:
		return TEXT("Strafe");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionPhaseFamily PhaseFamily)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Idle:
		return TEXT("Idle");
	case EProject_JLocomotionPhaseFamily::Start:
		return TEXT("Start");
	case EProject_JLocomotionPhaseFamily::Cycle:
		return TEXT("Cycle");
	case EProject_JLocomotionPhaseFamily::Stop:
		return TEXT("Stop");
	case EProject_JLocomotionPhaseFamily::Pivot:
		return TEXT("Pivot");
	case EProject_JLocomotionPhaseFamily::Turn:
		return TEXT("Turn");
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		return TEXT("TurnInPlace");
	case EProject_JLocomotionPhaseFamily::JumpStart:
		return TEXT("JumpStart");
	case EProject_JLocomotionPhaseFamily::Fall:
		return TEXT("Fall");
	case EProject_JLocomotionPhaseFamily::Landing:
		return TEXT("Landing");
	default:
		return TEXT("Unknown");
	}
}
}

UProject_JCharacterAnimInstance::UProject_JCharacterAnimInstance()
{
	bUseMultiThreadedAnimationUpdate = true;

	FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
	FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
	FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
	FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
	FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

	FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
	FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
	FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
	FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (NeedsOwnerReferenceRefresh())
	{
		CacheOwnerReferences();
	}

	if (ShouldSkipNativeUpdate(DeltaSeconds))
	{
		return;
	}

	ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
	ResetTrajectoryHistoryOnAccelerationStop(ThreadSafeData);
	PublishThreadSafeDataToProxy(ThreadSafeData);
}

FAnimInstanceProxy* UProject_JCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FProject_JCharacterAnimInstanceProxy(this);
}

void UProject_JCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UProject_JCharacterAnimInstance::HandleLocomotionAnimEvent(EProject_JLocomotionAnimEvent EventType)
{
	if (NeedsOwnerReferenceRefresh())
	{
		CacheOwnerReferences();
	}

	if (OwningPlayerCharacter)
	{
		if (UProject_JLocomotionAnimStateComponent* AnimState = OwningPlayerCharacter->GetLocomotionAnimStateComponent())
		{
			AnimState->HandleAnimationEvent(EventType);
		}
	}

	switch (EventType)
	{
	case EProject_JLocomotionAnimEvent::HitReactFinished:
		if (OwningPlayerCharacter)
		{
			OwningPlayerCharacter->bIsHitReacting = false;
		}
		break;
	case EProject_JLocomotionAnimEvent::AttackFinished:
		if (OwningPlayerCharacter)
		{
			OwningPlayerCharacter->bIsAttacking = false;
		}
		break;
	default:
		break;
	}
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::GetThreadSafeData() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
}

FTransformTrajectory UProject_JCharacterAnimInstance::GetThreadSafeTrajectory() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Trajectory;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimYaw() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimYaw;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimPitch() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimPitch;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimOffsetAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimOffsetAlpha;
}

FFootPlacementPlantSettings UProject_JCharacterAnimInstance::Get_FootPlacementPlantSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementPlantSettingsStops : Profile->FootPlacementPlantSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault;
}

FFootPlacementInterpolationSettings UProject_JCharacterAnimInstance::Get_FootPlacementInterpolationSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementInterpolationSettingsStops : Profile->FootPlacementInterpolationSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault;
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
}

FString UProject_JCharacterAnimInstance::GetAnimationDebugSummary() const
{
	const FProject_JAnimThreadSafeData& Data = ThreadSafeData;
	const bool bSprintAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsSprintLocomotionAllowed();
	const bool bJumpAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsJumpLocomotionAllowed();
	const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter ? OwningPlayerCharacter->GetWeaponAnimProfile() : nullptr;
	return FString::Printf(
		TEXT("Optimization Tier=%d UpdateData=%s FullChooser=%s FarOnly=%s MMInterval=%.3f ActivePSD=%s\n")
		TEXT("Weapon Profile=%s Stance=%s\n")
		TEXT("Movement GroundSpeed=%.1f VerticalSpeed=%.1f AccelRatio=%.2f HasTrajectory=%s StoppedAccel=%s\n")
		TEXT("Input Has=%s Size=%.2f Held=%.2f Turn=%.1f SharpTurn=%s MoveDir=%.1f\n")
		TEXT("Context Gait=%s Rotation=%s Phase=%s Starting=%s Pivoting=%s TurnInPlace=%s Spin=%s DesiredYaw=%.1f\n")
		TEXT("Policy SprintAllowed=%s JumpAllowed=%s Combat=%s Attack=%s Dodge=%s HitReact=%s\n")
		TEXT("Ground Mode=%d Start=%s Stop=%s WantsSprint=%s UseSprint=%s StartSprint=%s StopSprint=%s\n")
		TEXT("Air InAir=%s Jumping=%s FallOff=%s Landing=%s HeavyLand=%s LandMoving=%s LandSprint=%s\n")
		TEXT("Chooser Start=%s Stop=%s RunLoc=%s RemoteRunLoc=%s SprintLoc=%s Jump=%s Fall=%s Land=%s Combat=%s Remote=%s"),
		static_cast<int32>(CurrentOptimizationPolicy.Tier),
		CurrentOptimizationPolicy.bUpdateAnimationData ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFullChooserRows ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFarChooserRowsOnly ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.MotionMatchingUpdateInterval,
		*GetNameSafe(CurrentActivePoseSearchDatabase),
		*GetNameSafe(WeaponAnimProfile),
		WeaponAnimProfile ? ToDebugString(WeaponAnimProfile->WeaponStance) : TEXT("None"),
		Data.Movement.GroundSpeed,
		Data.Movement.VerticalSpeed,
		Data.Movement.AccelerationRatio,
		Data.Movement.bHasTrajectory ? TEXT("true") : TEXT("false"),
		Data.Movement.bStoppedAcceleratingThisFrame ? TEXT("true") : TEXT("false"),
		Data.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
		Data.Input.MoveInputSize,
		Data.Input.MoveInputHeldTime,
		Data.Input.MoveInputTurnAngle,
		Data.Input.bSharpTurnRequested ? TEXT("true") : TEXT("false"),
		Data.Input.MovementDirection,
		ToDebugString(Data.LocomotionContext.GaitIntent),
		ToDebugString(Data.LocomotionContext.RotationMode),
		ToDebugString(Data.LocomotionContext.PhaseFamily),
		Data.LocomotionContext.bIsStarting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldTurnInPlace ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldSpinTransition ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.DesiredFacingDeltaYaw,
		bSprintAllowed ? TEXT("true") : TEXT("false"),
		bJumpAllowed ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsAttacking ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsDodging ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsHitReacting ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Data.Ground.GroundMotionMode),
		Data.Ground.bStartRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bWantsSprint ? TEXT("true") : TEXT("false"),
		Data.Ground.bUseSprintLocomotion ? TEXT("true") : TEXT("false"),
		Data.Ground.bStartWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Air.bIsInAir ? TEXT("true") : TEXT("false"),
		Data.Air.bIsJumping ? TEXT("true") : TEXT("false"),
		Data.Air.bIsFallOffStart ? TEXT("true") : TEXT("false"),
		Data.Landing.bIsLanding ? TEXT("true") : TEXT("false"),
		Data.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
		bChooserStartRequested ? TEXT("true") : TEXT("false"),
		bChooserStopRequested ? TEXT("true") : TEXT("false"),
		bChooserUseRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseRemoteRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseSprintLocomotionRow ? TEXT("true") : TEXT("false"),
		bChooserUseJumpStart ? TEXT("true") : TEXT("false"),
		(bChooserUseFallOff || bChooserUseFallLoop) ? TEXT("true") : TEXT("false"),
		bChooserIsLanding ? TEXT("true") : TEXT("false"),
		bChooserIsCombatMode ? TEXT("true") : TEXT("false"),
		bChooserIsRemoteProxy ? TEXT("true") : TEXT("false"));
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::BuildThreadSafeData(float DeltaSeconds) const
{
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;

	if (!OwningCharacter)
	{
		return Data;
	}

	FillMovementThreadSafeData(Data);
	if (LocomotionAnimStateComponent)
	{
		FillLocomotionStateThreadSafeData(Data);
	}
	else
	{
		ApplyGenericMovementFallback(Data);
	}

	const bool bHasAimData = FillPlayerThreadSafeData(Data);
	FinalizeThreadSafeData(Data, bHasAimData);

	return Data;
}

void UProject_JCharacterAnimInstance::FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const FVector CharacterVelocity = OwningCharacter->GetVelocity();
	Data.Movement.Velocity = CharacterVelocity;
	Data.Movement.GroundSpeed = FVector(CharacterVelocity.X, CharacterVelocity.Y, 0.0f).Size();
	Data.Movement.VerticalSpeed = CharacterVelocity.Z;

	if (const UCharacterMovementComponent* MovementComponent = OwningCharacter->GetCharacterMovement())
	{
		Data.Movement.Acceleration = MovementComponent->GetCurrentAcceleration();
		Data.Movement.AccelerationDirection = Data.Movement.Acceleration.GetSafeNormal();
		Data.Movement.bIsAccelerating = Data.Movement.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		Data.Air.bIsInAir = MovementComponent->IsFalling();

		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Data.Movement.AccelerationRatio = FMath::Clamp(Data.Movement.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
	}

	Data.Movement.bWasAccelerating = ThreadSafeData.Movement.bIsAccelerating;
	Data.Movement.bStoppedAcceleratingThisFrame = Data.Movement.bWasAccelerating && !Data.Movement.bIsAccelerating;
}

void UProject_JCharacterAnimInstance::FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const UProject_JLocomotionAnimStateComponent* AnimState = LocomotionAnimStateComponent.Get();
	if (!AnimState)
	{
		return;
	}

	Data.Input.MoveInputSize = AnimState->MoveInputSize;
	Data.Input.MoveInputHeldTime = AnimState->MoveInputHeldTime;
	Data.Input.MoveInputTurnAngle = AnimState->MoveInputTurnAngle;
	Data.Input.MovementDirection = AnimState->MovementDirection;
	Data.Input.bHasMoveInput = AnimState->bHasMoveInput;
	Data.Input.bSharpTurnRequested = AnimState->bSharpTurnRequested;
	Data.Ground.bStartRequested = AnimState->bStartRequested || AnimState->bUseStartDatabase;
	Data.Ground.bStopRequested = AnimState->bStopRequested || AnimState->bUseStopDatabase;
	Data.Ground.bWantsSprint = AnimState->bWantsSprint;
	Data.Ground.bUseSprintLocomotion = AnimState->bUseSprintLocomotion;
	Data.Ground.bStartWasSprinting =
		AnimState->bStartWasSprinting ||
		(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	Data.Ground.bStopWasSprinting = AnimState->bStopWasSprinting;
	Data.Ground.GroundMotionMode = AnimState->GroundMotionMode;
	Data.Air.bIsJumping = AnimState->bIsJumping;
	Data.Air.bIsFallOffStart = AnimState->bIsFallOffStart;
	Data.Landing.bIsLanding = AnimState->bIsLanding || AnimState->bLandingRequested;
	Data.Landing.bUseHeavyLand = AnimState->bUseHeavyLand;
	Data.Landing.bLandWasSprinting = AnimState->bLandWasSprinting;
	Data.Landing.bLandWasMoving = AnimState->bLandWasMoving;
	Data.Landing.LastFallSpeed = AnimState->LastFallSpeed;
	Data.Landing.LandStartFallSpeed = AnimState->LandStartFallSpeed;
	Data.LocomotionContext.GaitIntent = AnimState->AuthoritativeContext.GaitIntent;
	Data.LocomotionContext.RotationMode = AnimState->AuthoritativeContext.RotationMode;
	Data.LocomotionContext.PhaseFamily = AnimState->DerivedLocomotionContext.PhaseFamily;
	Data.LocomotionContext.DesiredFacingDeltaYaw = AnimState->KinematicContext.DesiredFacingDeltaYaw;
	Data.LocomotionContext.bIsMoving = AnimState->DerivedLocomotionContext.bIsMoving;
	Data.LocomotionContext.bIsStarting = AnimState->DerivedLocomotionContext.bIsStarting;
	Data.LocomotionContext.bIsPivoting = AnimState->DerivedLocomotionContext.bIsPivoting;
	Data.LocomotionContext.bShouldTurnInPlace = AnimState->DerivedLocomotionContext.bShouldTurnInPlace;
	Data.LocomotionContext.bShouldSpinTransition = AnimState->DerivedLocomotionContext.bShouldSpinTransition;
}

void UProject_JCharacterAnimInstance::ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const
{
	const float MoveInputSpeedThreshold = GetEffectiveGenericMoveInputSpeedThreshold();
	Data.Input.bHasMoveInput = Data.Movement.bIsAccelerating || Data.Movement.GroundSpeed > MoveInputSpeedThreshold;
	Data.Ground.bUseSprintLocomotion = Data.Movement.GroundSpeed >= GetEffectiveSprintLocomotionSpeedThreshold();
	Data.Ground.GroundMotionMode = Data.Movement.GroundSpeed > MoveInputSpeedThreshold
		? EProject_JGroundMotionMode::Locomotion
		: EProject_JGroundMotionMode::Idle;
	Data.LocomotionContext.GaitIntent = Data.Ground.bUseSprintLocomotion
		? EProject_JLocomotionGaitIntent::Sprint
		: EProject_JLocomotionGaitIntent::Run;
	Data.LocomotionContext.PhaseFamily = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion
		? EProject_JLocomotionPhaseFamily::Cycle
		: EProject_JLocomotionPhaseFamily::Idle;
	Data.LocomotionContext.bIsMoving = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion;
}

bool UProject_JCharacterAnimInstance::FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return false;
	}

	Data.Ground.bWantsSprint =
		(Data.Ground.bWantsSprint || OwningPlayerCharacter->bIsSprinting) &&
		OwningPlayerCharacter->IsSprintLocomotionAllowed();
	Data.Ground.bStartWasSprinting =
		Data.Ground.bStartWasSprinting ||
		(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	Data.Ground.bUseSprintLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.Ground.bWantsSprint &&
		(Data.Input.bHasMoveInput || Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold());
	Data.Combat.bIsCombatMode = OwningPlayerCharacter->IsCombatModeActive();
	Data.Combat.bIsAttacking = OwningPlayerCharacter->IsAttacking();
	Data.Combat.bIsDodging = OwningPlayerCharacter->IsDodging();
	Data.Combat.bIsHitReacting = OwningPlayerCharacter->IsHitReacting();
	Data.Combat.bIsPlayingCombatIntro = OwningPlayerCharacter->bIsPlayingCombatIntro;

	if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
	{
		Data.Movement.Trajectory = TrajectoryComponent->GetTrajectory();
		Data.Movement.bHasTrajectory = !Data.Movement.Trajectory.Samples.IsEmpty();
	}

	if (OwningCharacter->GetController())
	{
		const FRotator ControlRotation = OwningCharacter->GetControlRotation();
		const FRotator ActorRotation = OwningCharacter->GetActorRotation();
		Data.Aim.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, ControlRotation.Yaw), -MaxAimYaw, MaxAimYaw);
		Data.Aim.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(ControlRotation.Pitch), -MaxAimPitch, MaxAimPitch);
		return true;
	}

	return false;
}

void UProject_JCharacterAnimInstance::FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const
{
	if (bHasAimData)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
	}
}

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data)
{
	const bool bMotionMatchingEnabled = OwningCharacter && !IsDedicatedServerAnimationContext();
	const bool bUpdateMotionMatchingThisFrame = bMotionMatchingEnabled && ShouldEvaluateMotionMatchingThisFrame(Data.DeltaTime);

	if (bUpdateMotionMatchingThisFrame)
	{
		// Chooser properties only need refreshing when we're actually re-evaluating the PSD this frame.
		// Skipping on throttled frames (mid/far distance, hidden remote) reduces Game Thread cost
		// proportionally to how aggressively the Motion Matching update interval is throttled.
		PublishChooserProperties(Data);
		CurrentActivePoseSearchDatabase = EvaluatePoseSearchDatabaseOnGameThread(Data);
	}
	if (!bMotionMatchingEnabled)
	{
		CurrentActivePoseSearchDatabase = nullptr;
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	ProjectProxy.QueueGameThreadData(Data, CurrentActivePoseSearchDatabase, bMotionMatchingEnabled, bUpdateMotionMatchingThisFrame);
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		return nullptr;
	}

	const UProject_JMotionMatchingAssetSet* AssetSet = OwningPlayerCharacter
		? OwningPlayerCharacter->GetMotionMatchingAssetSet()
		: nullptr;
	UPoseSearchDatabase* IdleDatabase = AssetSet && AssetSet->IdlePoseSearchDatabase
		? AssetSet->IdlePoseSearchDatabase.Get()
		: DefaultIdlePoseSearchDatabase.Get();
	UPoseSearchDatabase* LocomotionDatabase = AssetSet && AssetSet->DefaultPoseSearchDatabase
		? AssetSet->DefaultPoseSearchDatabase.Get()
		: DefaultPoseSearchDatabase.Get();
	UPoseSearchDatabase* SelectedDatabase = AssetSet
		? AssetSet->FindDatabaseForContext(
			Data.LocomotionContext.GaitIntent,
			Data.LocomotionContext.RotationMode,
			Data.LocomotionContext.PhaseFamily)
		: nullptr;
	if (!SelectedDatabase)
	{
		SelectedDatabase = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
			? IdleDatabase
			: LocomotionDatabase;
	}
	
	const UChooserTable* ChooserTable = AssetSet && AssetSet->MotionMatchingChooserTable
		? AssetSet->MotionMatchingChooserTable.Get()
		: MotionMatchingChooserTable.Get();

	bool bIsFarDistance = false;
	if (AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		bIsFarDistance = BaseChar->GetSignificance() >= 2.0f;
	}

	if (ShouldDisableMotionMatchingBeyondFarDistance() && bIsFarDistance)
	{
		return nullptr;
	}

	if (!ChooserTable)
	{
		return SelectedDatabase;
	}

	FChooserEvaluationContext ChooserContext;
	ChooserContext.AddObjectParam(const_cast<UProject_JCharacterAnimInstance*>(this));
	FChooserPlayerSettings ChooserPlayerSettings;
	ChooserContext.AddStructParam(ChooserPlayerSettings);

	const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(const_cast<UChooserTable*>(ChooserTable));
	if (!ChooserObject.IsValid())
	{
		return SelectedDatabase;
	}

	UObject* ResultObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
		ChooserContext,
		ChooserObject,
		UPoseSearchDatabase::StaticClass());

	if (UPoseSearchDatabase* ResultDatabase = Cast<UPoseSearchDatabase>(ResultObject))
	{
		SelectedDatabase = ResultDatabase;
	}

	return SelectedDatabase;
}

void UProject_JCharacterAnimInstance::PublishChooserProperties(const FProject_JAnimThreadSafeData& Data)
{
	// All accesses now use sub-struct paths; no legacy flat fields.
	const FProject_JAnimOptimizationPolicy OptimizationPolicy = BuildOptimizationPolicy();
	CurrentOptimizationPolicy = OptimizationPolicy;

	PublishChooserMovementProperties(Data);
	PublishChooserGroundProperties(Data);
	PublishChooserAirProperties(Data);
	PublishChooserLandingProperties(Data);
	PublishChooserCombatProperties(Data);

	if (OptimizationPolicy.bUseFarChooserRowsOnly)
	{
		ApplyFarChooserOverrides(Data);
	}
}

void UProject_JCharacterAnimInstance::PublishChooserMovementProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserGroundSpeed = Data.Movement.GroundSpeed;
	ChooserVerticalSpeed = Data.Movement.VerticalSpeed;
	ChooserAccelerationRatio = Data.Movement.AccelerationRatio;
	ChooserMoveInputSize = Data.Input.MoveInputSize;
	ChooserMoveInputHeldTime = Data.Input.MoveInputHeldTime;
	ChooserMoveInputTurnAngle = Data.Input.MoveInputTurnAngle;
	bChooserHasMoveInput = Data.Input.bHasMoveInput;
	bChooserSharpTurnRequested = Data.Input.bSharpTurnRequested;
	bChooserIsRemoteProxy = OwningPlayerCharacter && !IsLocallyControlledCharacter();
}

void UProject_JCharacterAnimInstance::PublishChooserGroundProperties(const FProject_JAnimThreadSafeData& Data)
{
	const bool bDerivedStart = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Start;
	const bool bDerivedStop = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop;
	const bool bDerivedCycle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle;
	const bool bDerivedIdle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle;

	bChooserStartRequested = Data.Ground.bStartRequested || bDerivedStart;
	bChooserStopRequested = Data.Ground.bStopRequested || bDerivedStop;
	bChooserWantsSprint = Data.Ground.bWantsSprint;
	bChooserUseSprintLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserUseRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && !bChooserIsRemoteProxy;
	bChooserUseRemoteRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && bChooserIsRemoteProxy;
	bChooserUseSprintStart = bChooserStartRequested && Data.Ground.bStartWasSprinting;
	bChooserUseRunStop = bChooserStopRequested && !Data.Ground.bStopWasSprinting;
	bChooserUseSprintStop = bChooserStopRequested && Data.Ground.bStopWasSprinting;
	bChooserUseRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		!bChooserIsRemoteProxy;
	bChooserUseRemoteRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		bChooserIsRemoteProxy;
	bChooserUseSprintLocomotionRow =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserStartWasSprinting = Data.Ground.bStartWasSprinting;
	bChooserStopWasSprinting = Data.Ground.bStopWasSprinting;
	bChooserIsIdle = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle || bDerivedIdle;
	ChooserGroundMotionMode = Data.Ground.GroundMotionMode;
	ChooserGaitIntent = Data.LocomotionContext.GaitIntent;
	ChooserRotationMode = Data.LocomotionContext.RotationMode;
	ChooserPhaseFamily = Data.LocomotionContext.PhaseFamily;
	ChooserDesiredFacingDeltaYaw = Data.LocomotionContext.DesiredFacingDeltaYaw;
	bChooserIsStartingDerived = Data.LocomotionContext.bIsStarting;
	bChooserIsPivoting = Data.LocomotionContext.bIsPivoting;
	bChooserShouldTurnInPlace = Data.LocomotionContext.bShouldTurnInPlace;
	bChooserShouldSpinTransition = Data.LocomotionContext.bShouldSpinTransition;
}

void UProject_JCharacterAnimInstance::PublishChooserAirProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserUseJumpStart =
		Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallOff =
		Data.Air.bIsFallOffStart &&
		!Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallLoop =
		Data.Air.bIsInAir &&
		!Data.Air.bIsJumping &&
		!Data.Air.bIsFallOffStart &&
		!Data.Landing.bIsLanding;
	bChooserIsInAir = Data.Air.bIsInAir;
	bChooserIsJumping = Data.Air.bIsJumping;
}

void UProject_JCharacterAnimInstance::PublishChooserLandingProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserLastFallSpeed = Data.Landing.LastFallSpeed;
	ChooserLandStartFallSpeed = Data.Landing.LandStartFallSpeed;
	bChooserUseLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand;
	bChooserUseHeavyLandRow =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand;
	bChooserUseStandLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseStandHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseRunLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserUseRunHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserLandWasSprinting = Data.Landing.bLandWasSprinting;
	bChooserLandWasMoving = Data.Landing.bLandWasMoving;
	bChooserIsLanding = Data.Landing.bIsLanding;
	bChooserUseHeavyLand = Data.Landing.bUseHeavyLand;
}

void UProject_JCharacterAnimInstance::PublishChooserCombatProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserIsCombatMode = Data.Combat.bIsCombatMode;
}

void UProject_JCharacterAnimInstance::ApplyFarChooserOverrides(const FProject_JAnimThreadSafeData& Data)
{
	const bool bUseFarLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.Air.bIsInAir &&
		!Data.Air.bIsJumping;
	const bool bUseFarRunLocomotion = bUseFarLocomotion && !Data.Ground.bUseSprintLocomotion;
	const bool bUseFarSprintLocomotion = bUseFarLocomotion && Data.Ground.bUseSprintLocomotion;

	bChooserStartRequested = false;
	bChooserStopRequested = false;
	bChooserSharpTurnRequested = false;
	bChooserUseRunStart = false;
	bChooserUseRemoteRunStart = false;
	bChooserUseSprintStart = false;
	bChooserUseRunStop = false;
	bChooserUseSprintStop = false;
	bChooserUseFallOff = false;
	bChooserUseLightLand = false;
	bChooserUseHeavyLandRow = false;
	bChooserUseStandLightLand = false;
	bChooserUseStandHeavyLand = false;
	bChooserUseRunLightLand = false;
	bChooserUseSprintLightLand = false;
	bChooserUseRunHeavyLand = false;
	bChooserUseSprintHeavyLand = false;
	bChooserIsLanding = false;
	bChooserUseHeavyLand = false;
	bChooserLandWasMoving = false;
	bChooserLandWasSprinting = false;
	bChooserUseSprintLocomotion = bUseFarSprintLocomotion;
	bChooserUseRunLocomotion = false;
	bChooserUseRemoteRunLocomotion = bUseFarRunLocomotion;
	bChooserUseSprintLocomotionRow = bUseFarSprintLocomotion;
	bChooserUseJumpStart = Data.Air.bIsJumping;
	bChooserUseFallLoop = Data.Air.bIsInAir && !Data.Air.bIsJumping;
	bChooserIsIdle = !Data.Air.bIsInAir && Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return false;
	}

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval <= 0.0f)
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return true;
	}

	MotionMatchingUpdateAccumulator += DeltaSeconds;
	if (MotionMatchingUpdateAccumulator < UpdateInterval)
	{
		return false;
	}

	MotionMatchingUpdateAccumulator = 0.0f;
	return true;
}

FProject_JAnimOptimizationPolicy UProject_JCharacterAnimInstance::BuildOptimizationPolicy() const
{
	FProject_JAnimOptimizationPolicy Policy;
	if (!OwningCharacter)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		return Policy;
	}

	if (IsLocallyControlledCharacter())
	{
		return Policy;
	}

	const bool bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
	if (!bRecentlyRendered)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		Policy.MotionMatchingUpdateInterval = GetEffectiveHiddenRemoteUpdateInterval();
		return Policy;
	}

	if (const AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		const float Significance = BaseChar->GetSignificance();
		if (Significance <= 0.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Near;
			return Policy;
		}

		if (Significance <= 1.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Mid;
			Policy.MotionMatchingUpdateInterval = GetEffectiveMidMotionMatchingUpdateInterval();
			return Policy;
		}

		Policy.Tier = EProject_JAnimBudgetTier::Far;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = true;
		Policy.MotionMatchingUpdateInterval = GetEffectiveFarMotionMatchingUpdateInterval();
		return Policy;
	}

	Policy.Tier = EProject_JAnimBudgetTier::Near;
	return Policy;
}

float UProject_JCharacterAnimInstance::CalculateMotionMatchingUpdateInterval() const
{
	if (!OwningCharacter || IsLocallyControlledCharacter())
	{
		return 0.0f;
	}

	if (AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		const float Significance = BaseChar->GetSignificance();
		if (Significance <= 0.0f)
		{
			return 0.0f; // Near
		}
		if (Significance <= 1.0f)
		{
			return GetEffectiveMidMotionMatchingUpdateInterval(); // Mid
		}
		
		return GetEffectiveFarMotionMatchingUpdateInterval(); // Far
	}

	return 0.0f;
}

void UProject_JCharacterAnimInstance::ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const
{
	if (!Data.Movement.bStoppedAcceleratingThisFrame)
	{
		return;
	}

	if (OwningPlayerCharacter)
	{
		if (UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
		{
			TrajectoryComponent->ResetTrajectoryHistory();
			return;
		}
	}

	if (OwningCharacter)
	{
		if (UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningCharacter->FindComponentByClass<UProject_JMotionMatchingTrajectoryComponent>())
		{
			TrajectoryComponent->ResetTrajectoryHistory();
		}
	}
}

float UProject_JCharacterAnimInstance::CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const
{
	if (Data.Combat.bIsCombatMode)
	{
		return GetEffectiveCombatAimAlpha();
	}

	if (Data.Ground.bUseSprintLocomotion)
	{
		return SprintAimAlpha;
	}

	return Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold() ? MovingAimAlpha : StandingAimAlpha;
}

bool UProject_JCharacterAnimInstance::ShouldSkipNativeUpdate(float DeltaSeconds)
{
	if (!OwningCharacter)
	{
		ThreadSafeData = FProject_JAnimThreadSafeData();
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	if (bSkipDedicatedServerAnimationDataUpdate && IsDedicatedServerAnimationContext())
	{
		ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
	if (CurrentOptimizationPolicy.bUpdateAnimationData)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return false;
	}

	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval > 0.0f)
	{
		HiddenRemoteUpdateAccumulator += DeltaSeconds;
		if (HiddenRemoteUpdateAccumulator >= UpdateInterval)
		{
			HiddenRemoteUpdateAccumulator = 0.0f;
			return false;
		}
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, true, false);
	return true;
}

const UProject_JLocomotionProfile* UProject_JCharacterAnimInstance::GetLocomotionProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionProfile() : nullptr;
}

float UProject_JCharacterAnimInstance::GetEffectiveGenericMoveInputSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->GenericMoveInputSpeedThreshold;
	}

	return GenericMoveInputSpeedThreshold;
}

float UProject_JCharacterAnimInstance::GetEffectiveSprintLocomotionSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->SprintLocomotionSpeedThreshold;
	}

	return SprintLocomotionSpeedThreshold;
}

float UProject_JCharacterAnimInstance::GetEffectiveHiddenRemoteUpdateInterval() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->AnimInstanceHiddenRemoteUpdateInterval;
	}

	return HiddenRemoteUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveNearMotionMatchingDistance() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->NearMotionMatchingDistance;
	}

	return NearMotionMatchingDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingDistance() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->MidMotionMatchingDistance;
	}

	return MidMotionMatchingDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveFarMotionMatchingDistance() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->FarMotionMatchingDistance;
	}

	return FarMotionMatchingDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingUpdateInterval() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->MidMotionMatchingUpdateInterval;
	}

	return MidMotionMatchingUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveFarMotionMatchingUpdateInterval() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->FarMotionMatchingUpdateInterval;
	}

	return FarMotionMatchingUpdateInterval;
}

bool UProject_JCharacterAnimInstance::ShouldDisableMotionMatchingBeyondFarDistance() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->bDisableMotionMatchingBeyondFarDistance;
	}

	return bDisableMotionMatchingBeyondFarDistance;
}

const UProject_JCombatAnimProfile* UProject_JCharacterAnimInstance::GetCombatAnimProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetCombatAnimProfile() : nullptr;
}

float UProject_JCharacterAnimInstance::GetEffectiveCombatAimAlpha() const
{
	if (const UProject_JCombatAnimProfile* Profile = GetCombatAnimProfile())
	{
		return Profile->CombatAimAlpha;
	}

	return CombatAimAlpha;
}
