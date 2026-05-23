// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "StructUtils/InstancedStruct.h"

void FProject_JAnimThreadSafeData::SyncLegacyFieldsFromStructuredData()
{
	Velocity = Movement.Velocity;
	Acceleration = Movement.Acceleration;
	AccelerationDirection = Movement.AccelerationDirection;
	Trajectory = Movement.Trajectory;
	AccelerationRatio = Movement.AccelerationRatio;
	GroundSpeed = Movement.GroundSpeed;
	VerticalSpeed = Movement.VerticalSpeed;
	bIsAccelerating = Movement.bIsAccelerating;
	bWasAccelerating = Movement.bWasAccelerating;
	bStoppedAcceleratingThisFrame = Movement.bStoppedAcceleratingThisFrame;
	bHasTrajectory = Movement.bHasTrajectory;

	MoveInputSize = Input.MoveInputSize;
	MoveInputHeldTime = Input.MoveInputHeldTime;
	MoveInputTurnAngle = Input.MoveInputTurnAngle;
	MovementDirection = Input.MovementDirection;
	bHasMoveInput = Input.bHasMoveInput;
	bSharpTurnRequested = Input.bSharpTurnRequested;

	bStartRequested = Ground.bStartRequested;
	bStopRequested = Ground.bStopRequested;
	bWantsSprint = Ground.bWantsSprint;
	bUseSprintLocomotion = Ground.bUseSprintLocomotion;
	bStartWasSprinting = Ground.bStartWasSprinting;
	bStopWasSprinting = Ground.bStopWasSprinting;
	GroundMotionMode = Ground.GroundMotionMode;

	bIsInAir = Air.bIsInAir;
	bIsJumping = Air.bIsJumping;
	bIsFallOffStart = Air.bIsFallOffStart;

	LastFallSpeed = Landing.LastFallSpeed;
	LandStartFallSpeed = Landing.LandStartFallSpeed;
	bIsLanding = Landing.bIsLanding;
	bUseHeavyLand = Landing.bUseHeavyLand;
	bLandWasSprinting = Landing.bLandWasSprinting;
	bLandWasMoving = Landing.bLandWasMoving;

	bIsCombatMode = Combat.bIsCombatMode;
	bIsAttacking = Combat.bIsAttacking;
	bIsDodging = Combat.bIsDodging;
	bIsHitReacting = Combat.bIsHitReacting;
	bIsPlayingCombatIntro = Combat.bIsPlayingCombatIntro;

	AimYaw = Aim.AimYaw;
	AimPitch = Aim.AimPitch;
	AimOffsetAlpha = Aim.AimOffsetAlpha;
}

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
	if (!bUpdateMotionMatchingThisFrame)
	{
		return;
	}

	ApplySelectedDatabaseToNativeNode();
	NativePoseHistoryNode.TransformTrajectory = ThreadSafeData.Trajectory;
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
	return Data.Ground.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault;
}

FFootPlacementInterpolationSettings UProject_JCharacterAnimInstance::Get_FootPlacementInterpolationSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.Ground.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault;
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
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

	Data.Movement.bWasAccelerating = ThreadSafeData.Movement.bIsAccelerating || ThreadSafeData.bIsAccelerating;
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
}

void UProject_JCharacterAnimInstance::ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const
{
	const float MoveInputSpeedThreshold = GetEffectiveGenericMoveInputSpeedThreshold();
	Data.Input.bHasMoveInput = Data.Movement.bIsAccelerating || Data.Movement.GroundSpeed > MoveInputSpeedThreshold;
	Data.Ground.bUseSprintLocomotion = Data.Movement.GroundSpeed >= GetEffectiveSprintLocomotionSpeedThreshold();
	Data.Ground.GroundMotionMode = Data.Movement.GroundSpeed > MoveInputSpeedThreshold
		? EProject_JGroundMotionMode::Locomotion
		: EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return false;
	}

	Data.Ground.bWantsSprint = Data.Ground.bWantsSprint || OwningPlayerCharacter->bIsSprinting;
	Data.Ground.bStartWasSprinting =
		Data.Ground.bStartWasSprinting ||
		(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	Data.Ground.bUseSprintLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.Ground.bWantsSprint &&
		(Data.Input.bHasMoveInput || Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold());
	Data.Combat.bIsCombatMode = OwningPlayerCharacter->bIsCombatMode;
	Data.Combat.bIsAttacking = OwningPlayerCharacter->bIsAttacking;
	Data.Combat.bIsDodging = OwningPlayerCharacter->bIsDodging;
	Data.Combat.bIsHitReacting = OwningPlayerCharacter->bIsHitReacting;
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
	Data.SyncLegacyFieldsFromStructuredData();
	if (bHasAimData)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
		Data.SyncLegacyFieldsFromStructuredData();
	}
}

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data)
{
	const bool bMotionMatchingEnabled = OwningCharacter && !IsDedicatedServerAnimationContext();
	const bool bUpdateMotionMatchingThisFrame = bMotionMatchingEnabled && ShouldEvaluateMotionMatchingThisFrame(Data.DeltaTime);
	PublishChooserProperties(Data);

	if (bUpdateMotionMatchingThisFrame)
	{
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
		: (OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingIdleDatabase
			? OwningPlayerCharacter->MotionMatchingIdleDatabase.Get()
			: DefaultIdlePoseSearchDatabase.Get());
	UPoseSearchDatabase* LocomotionDatabase = AssetSet && AssetSet->DefaultPoseSearchDatabase
		? AssetSet->DefaultPoseSearchDatabase.Get()
		: (OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingDefaultDatabase
			? OwningPlayerCharacter->MotionMatchingDefaultDatabase.Get()
			: DefaultPoseSearchDatabase.Get());
	UPoseSearchDatabase* SelectedDatabase = Data.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
		? IdleDatabase
		: LocomotionDatabase;
	const UChooserTable* ChooserTable = AssetSet && AssetSet->MotionMatchingChooserTable
		? AssetSet->MotionMatchingChooserTable.Get()
		: (OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingChooserTable
			? OwningPlayerCharacter->MotionMatchingChooserTable.Get()
			: MotionMatchingChooserTable.Get());

	if (ShouldDisableMotionMatchingBeyondFarDistance() && CalculateViewerDistanceSquared() > FMath::Square(GetEffectiveFarMotionMatchingDistance()))
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
	ChooserGroundSpeed = Data.GroundSpeed;
	ChooserVerticalSpeed = Data.VerticalSpeed;
	ChooserAccelerationRatio = Data.AccelerationRatio;
	ChooserMoveInputSize = Data.MoveInputSize;
	ChooserMoveInputHeldTime = Data.MoveInputHeldTime;
	ChooserMoveInputTurnAngle = Data.MoveInputTurnAngle;
	ChooserLastFallSpeed = Data.LastFallSpeed;
	ChooserLandStartFallSpeed = Data.LandStartFallSpeed;
	bChooserHasMoveInput = Data.bHasMoveInput;
	bChooserStartRequested = Data.bStartRequested;
	bChooserStopRequested = Data.bStopRequested;
	bChooserSharpTurnRequested = Data.bSharpTurnRequested;
	bChooserWantsSprint = Data.bWantsSprint;
	bChooserIsRemoteProxy = OwningPlayerCharacter && !IsLocallyControlledCharacter();
	bChooserUseSprintLocomotion =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.bUseSprintLocomotion;
	bChooserUseRunStart = Data.bStartRequested && !Data.bStartWasSprinting && !bChooserIsRemoteProxy;
	bChooserUseRemoteRunStart = Data.bStartRequested && !Data.bStartWasSprinting && bChooserIsRemoteProxy;
	bChooserUseSprintStart = Data.bStartRequested && Data.bStartWasSprinting;
	bChooserUseRunStop = Data.bStopRequested && !Data.bStopWasSprinting;
	bChooserUseSprintStop = Data.bStopRequested && Data.bStopWasSprinting;
	bChooserUseRunLocomotion =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.bUseSprintLocomotion &&
		!bChooserIsRemoteProxy;
	bChooserUseRemoteRunLocomotion =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.bUseSprintLocomotion &&
		bChooserIsRemoteProxy;
	bChooserUseSprintLocomotionRow =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.bUseSprintLocomotion;
	bChooserUseJumpStart =
		Data.bIsJumping &&
		!Data.bIsLanding;
	bChooserUseFallOff =
		Data.bIsFallOffStart &&
		!Data.bIsJumping &&
		!Data.bIsLanding;
	bChooserUseFallLoop =
		Data.bIsInAir &&
		!Data.bIsJumping &&
		!Data.bIsFallOffStart &&
		!Data.bIsLanding;
	bChooserUseLightLand =
		Data.bIsLanding &&
		!Data.bUseHeavyLand;
	bChooserUseHeavyLandRow =
		Data.bIsLanding &&
		Data.bUseHeavyLand;
	bChooserUseStandLightLand =
		Data.bIsLanding &&
		!Data.bUseHeavyLand &&
		!Data.bLandWasMoving;
	bChooserUseStandHeavyLand =
		Data.bIsLanding &&
		Data.bUseHeavyLand &&
		!Data.bLandWasMoving;
	bChooserUseRunLightLand =
		Data.bIsLanding &&
		!Data.bUseHeavyLand &&
		Data.bLandWasMoving &&
		!Data.bLandWasSprinting;
	bChooserUseSprintLightLand =
		Data.bIsLanding &&
		!Data.bUseHeavyLand &&
		Data.bLandWasMoving &&
		Data.bLandWasSprinting;
	bChooserUseRunHeavyLand =
		Data.bIsLanding &&
		Data.bUseHeavyLand &&
		Data.bLandWasMoving &&
		!Data.bLandWasSprinting;
	bChooserUseSprintHeavyLand =
		Data.bIsLanding &&
		Data.bUseHeavyLand &&
		Data.bLandWasMoving &&
		Data.bLandWasSprinting;
	bChooserLandWasSprinting = Data.bLandWasSprinting;
	bChooserLandWasMoving = Data.bLandWasMoving;
	bChooserStartWasSprinting = Data.bStartWasSprinting;
	bChooserStopWasSprinting = Data.bStopWasSprinting;
	bChooserIsInAir = Data.bIsInAir;
	bChooserIsJumping = Data.bIsJumping;
	bChooserIsLanding = Data.bIsLanding;
	bChooserUseHeavyLand = Data.bUseHeavyLand;
	bChooserIsCombatMode = Data.bIsCombatMode;
	bChooserIsIdle = Data.GroundMotionMode == EProject_JGroundMotionMode::Idle;
	ChooserGroundMotionMode = Data.GroundMotionMode;
}

bool UProject_JCharacterAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return false;
	}

	const float UpdateInterval = CalculateMotionMatchingUpdateInterval();
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

float UProject_JCharacterAnimInstance::CalculateMotionMatchingUpdateInterval() const
{
	if (!OwningCharacter || IsLocallyControlledCharacter())
	{
		return 0.0f;
	}

	const float DistanceSquared = CalculateViewerDistanceSquared();
	if (DistanceSquared <= FMath::Square(GetEffectiveNearMotionMatchingDistance()))
	{
		return 0.0f;
	}

	if (DistanceSquared <= FMath::Square(GetEffectiveMidMotionMatchingDistance()))
	{
		return GetEffectiveMidMotionMatchingUpdateInterval();
	}

	return GetEffectiveFarMotionMatchingUpdateInterval();
}

void UProject_JCharacterAnimInstance::ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const
{
	if (!Data.bStoppedAcceleratingThisFrame)
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
	if (Data.bIsCombatMode)
	{
		return CombatAimAlpha;
	}

	if (Data.bUseSprintLocomotion)
	{
		return SprintAimAlpha;
	}

	return Data.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold() ? MovingAimAlpha : StandingAimAlpha;
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

	const bool bLocallyControlled = IsLocallyControlledCharacter();
	const bool bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
	const float EffectiveHiddenRemoteUpdateInterval = GetEffectiveHiddenRemoteUpdateInterval();
	if (bLocallyControlled || bRecentlyRendered || EffectiveHiddenRemoteUpdateInterval <= 0.0f)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return false;
	}

	HiddenRemoteUpdateAccumulator += DeltaSeconds;
	if (HiddenRemoteUpdateAccumulator < EffectiveHiddenRemoteUpdateInterval)
	{
		FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
		ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, true, false);
		return true;
	}

	HiddenRemoteUpdateAccumulator = 0.0f;
	return false;
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
