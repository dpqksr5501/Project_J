// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
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
}

void UProject_JCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CacheOwnerReferences();
}

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwningPawn || OwningPawn != TryGetPawnOwner())
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
	if (!OwningPawn || OwningPawn != TryGetPawnOwner())
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

const FProject_JAnimThreadSafeData& UProject_JCharacterAnimInstance::GetThreadSafeData() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
}

void UProject_JCharacterAnimInstance::CacheOwnerReferences()
{
	OwningPawn = TryGetPawnOwner();
	OwningCharacter = Cast<ACharacter>(OwningPawn);
	OwningPlayerCharacter = Cast<AProject_JPlayerCharacter>(OwningCharacter);
	LocomotionAnimStateComponent = OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionAnimStateComponent() : nullptr;
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::BuildThreadSafeData(float DeltaSeconds) const
{
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;

	if (!OwningCharacter)
	{
		return Data;
	}

	CopyMovementThreadSafeData(Data);
	if (LocomotionAnimStateComponent)
	{
		CopyAnimStateThreadSafeData(Data);
	}
	else
	{
		ApplyGenericMovementFallback(Data);
	}

	const bool bHasAimData = CopyPlayerThreadSafeData(Data);

	Data.SyncLegacyFieldsFromStructuredData();
	if (bHasAimData)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
		Data.SyncLegacyFieldsFromStructuredData();
	}

	return Data;
}

void UProject_JCharacterAnimInstance::CopyMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const
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

void UProject_JCharacterAnimInstance::CopyAnimStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const
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
	Data.Input.bHasMoveInput = Data.Movement.bIsAccelerating || Data.Movement.GroundSpeed > GenericMoveInputSpeedThreshold;
	Data.Ground.bUseSprintLocomotion = Data.Movement.GroundSpeed >= SprintLocomotionSpeedThreshold;
	Data.Ground.GroundMotionMode = Data.Movement.GroundSpeed > GenericMoveInputSpeedThreshold
		? EProject_JGroundMotionMode::Locomotion
		: EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::CopyPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
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
		(Data.Input.bHasMoveInput || Data.Movement.GroundSpeed > GenericMoveInputSpeedThreshold);
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

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data)
{
	const bool bDedicatedServer = OwningCharacter && OwningCharacter->GetNetMode() == NM_DedicatedServer;
	const bool bMotionMatchingEnabled = OwningCharacter && !bDedicatedServer;
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
	if (!OwningCharacter || OwningCharacter->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	UPoseSearchDatabase* IdleDatabase = OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingIdleDatabase
		? OwningPlayerCharacter->MotionMatchingIdleDatabase.Get()
		: DefaultIdlePoseSearchDatabase.Get();
	UPoseSearchDatabase* LocomotionDatabase = OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingDefaultDatabase
		? OwningPlayerCharacter->MotionMatchingDefaultDatabase.Get()
		: DefaultPoseSearchDatabase.Get();
	UPoseSearchDatabase* SelectedDatabase = Data.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
		? IdleDatabase
		: LocomotionDatabase;
	const UChooserTable* ChooserTable = OwningPlayerCharacter && OwningPlayerCharacter->MotionMatchingChooserTable
		? OwningPlayerCharacter->MotionMatchingChooserTable.Get()
		: MotionMatchingChooserTable.Get();

	if (bDisableMotionMatchingBeyondFarDistance && CalculateViewerDistanceSquared() > FMath::Square(FarMotionMatchingDistance))
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
	bChooserIsRemoteProxy = OwningPlayerCharacter && !OwningPlayerCharacter->IsLocallyControlled();
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
	if (!OwningCharacter || OwningCharacter->GetNetMode() == NM_DedicatedServer)
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
	if (!OwningCharacter || OwningCharacter->IsLocallyControlled())
	{
		return 0.0f;
	}

	const float DistanceSquared = CalculateViewerDistanceSquared();
	if (DistanceSquared <= FMath::Square(NearMotionMatchingDistance))
	{
		return 0.0f;
	}

	if (DistanceSquared <= FMath::Square(MidMotionMatchingDistance))
	{
		return MidMotionMatchingUpdateInterval;
	}

	return FarMotionMatchingUpdateInterval;
}

float UProject_JCharacterAnimInstance::CalculateViewerDistanceSquared() const
{
	const UWorld* World = OwningCharacter ? OwningCharacter->GetWorld() : nullptr;
	if (!World || !OwningCharacter)
	{
		return 0.0f;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return 0.0f;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	return FVector::DistSquared(ViewLocation, OwningCharacter->GetActorLocation());
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

	return Data.GroundSpeed > GenericMoveInputSpeedThreshold ? MovingAimAlpha : StandingAimAlpha;
}

bool UProject_JCharacterAnimInstance::ShouldSkipNativeUpdate(float DeltaSeconds)
{
	if (!OwningCharacter)
	{
		ThreadSafeData = FProject_JAnimThreadSafeData();
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	if (bSkipDedicatedServerAnimationDataUpdate && OwningCharacter->GetNetMode() == NM_DedicatedServer)
	{
		ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	const bool bLocallyControlled = OwningCharacter->IsLocallyControlled();
	const bool bRecentlyRendered = !OwningCharacter->GetMesh() || OwningCharacter->GetMesh()->WasRecentlyRendered(RecentlyRenderedTolerance);
	if (bLocallyControlled || bRecentlyRendered || HiddenRemoteUpdateInterval <= 0.0f)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return false;
	}

	HiddenRemoteUpdateAccumulator += DeltaSeconds;
	if (HiddenRemoteUpdateAccumulator < HiddenRemoteUpdateInterval)
	{
		FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
		ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, true, false);
		return true;
	}

	HiddenRemoteUpdateAccumulator = 0.0f;
	return false;
}
