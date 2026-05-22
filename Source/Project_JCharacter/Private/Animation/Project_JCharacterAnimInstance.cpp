// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "ChooserFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "StructUtils/InstancedStruct.h"

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

	const FVector CharacterVelocity = OwningCharacter->GetVelocity();
	Data.Velocity = CharacterVelocity;
	Data.GroundSpeed = FVector(CharacterVelocity.X, CharacterVelocity.Y, 0.0f).Size();
	Data.VerticalSpeed = CharacterVelocity.Z;

	if (const UCharacterMovementComponent* MovementComponent = OwningCharacter->GetCharacterMovement())
	{
		Data.Acceleration = MovementComponent->GetCurrentAcceleration();
		Data.AccelerationDirection = Data.Acceleration.GetSafeNormal();
		Data.bIsAccelerating = Data.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		Data.bIsInAir = MovementComponent->IsFalling();

		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Data.AccelerationRatio = FMath::Clamp(Data.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
	}

	Data.bWasAccelerating = ThreadSafeData.bIsAccelerating;
	Data.bStoppedAcceleratingThisFrame = Data.bWasAccelerating && !Data.bIsAccelerating;

	if (const UProject_JLocomotionAnimStateComponent* AnimState = LocomotionAnimStateComponent.Get())
	{
		Data.MoveInputSize = AnimState->MoveInputSize;
		Data.MoveInputHeldTime = AnimState->MoveInputHeldTime;
		Data.MoveInputTurnAngle = AnimState->MoveInputTurnAngle;
		Data.MovementDirection = AnimState->MovementDirection;
		Data.bHasMoveInput = AnimState->bHasMoveInput;
		Data.bSharpTurnRequested = AnimState->bSharpTurnRequested;
		Data.bStartRequested = AnimState->bStartRequested || AnimState->bUseStartDatabase;
		Data.bStopRequested = AnimState->bStopRequested || AnimState->bUseStopDatabase;
		Data.bWantsSprint = AnimState->bWantsSprint;
		Data.bUseSprintLocomotion = AnimState->bUseSprintLocomotion;
		Data.bStartWasSprinting =
			AnimState->bStartWasSprinting ||
			(Data.bStartRequested && Data.bWantsSprint && Data.bHasMoveInput);
		Data.bStopWasSprinting = AnimState->bStopWasSprinting;
		Data.bIsJumping = AnimState->bIsJumping;
		Data.bIsLanding = AnimState->bIsLanding || AnimState->bLandingRequested;
		Data.bUseHeavyLand = AnimState->bUseHeavyLand;
		Data.LastFallSpeed = AnimState->LastFallSpeed;
		Data.LandStartFallSpeed = AnimState->LandStartFallSpeed;
		Data.GroundMotionMode = AnimState->GroundMotionMode;
	}
	else
	{
		Data.bHasMoveInput = Data.bIsAccelerating || Data.GroundSpeed > GenericMoveInputSpeedThreshold;
		Data.bUseSprintLocomotion = Data.GroundSpeed >= SprintLocomotionSpeedThreshold;
		Data.GroundMotionMode = Data.GroundSpeed > GenericMoveInputSpeedThreshold
			? EProject_JGroundMotionMode::Locomotion
			: EProject_JGroundMotionMode::Idle;
	}

	if (OwningPlayerCharacter)
	{
		Data.bWantsSprint = Data.bWantsSprint || OwningPlayerCharacter->bIsSprinting;
		Data.bStartWasSprinting =
			Data.bStartWasSprinting ||
			(Data.bStartRequested && Data.bWantsSprint && Data.bHasMoveInput);
		Data.bUseSprintLocomotion =
			Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
			Data.bWantsSprint &&
			(Data.bHasMoveInput || Data.GroundSpeed > GenericMoveInputSpeedThreshold);
		Data.bIsCombatMode = OwningPlayerCharacter->bIsCombatMode;
		Data.bIsAttacking = OwningPlayerCharacter->bIsAttacking;
		Data.bIsDodging = OwningPlayerCharacter->bIsDodging;
		Data.bIsHitReacting = OwningPlayerCharacter->bIsHitReacting;
		Data.bIsPlayingCombatIntro = OwningPlayerCharacter->bIsPlayingCombatIntro;

		if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
		{
			Data.Trajectory = TrajectoryComponent->GetTrajectory();
			Data.bHasTrajectory = !Data.Trajectory.Samples.IsEmpty();
		}

		if (OwningCharacter->GetController())
		{
			const FRotator ControlRotation = OwningCharacter->GetControlRotation();
			const FRotator ActorRotation = OwningCharacter->GetActorRotation();
			Data.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, ControlRotation.Yaw), -MaxAimYaw, MaxAimYaw);
			Data.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(ControlRotation.Pitch), -MaxAimPitch, MaxAimPitch);
			Data.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
		}
	}

	return Data;
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
	bChooserUseSprintLocomotion =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.bUseSprintLocomotion;
	bChooserUseRunStart = Data.bStartRequested && !Data.bStartWasSprinting;
	bChooserUseSprintStart = Data.bStartRequested && Data.bStartWasSprinting;
	bChooserUseRunStop = Data.bStopRequested && !Data.bStopWasSprinting;
	bChooserUseSprintStop = Data.bStopRequested && Data.bStopWasSprinting;
	bChooserUseRunLocomotion =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.bUseSprintLocomotion;
	bChooserUseSprintLocomotionRow =
		Data.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.bUseSprintLocomotion;
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
