#include "Animation/Project_JPlayerAnimInstance.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMoverComponent.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMovementQueries.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoverPoseSearchTrajectoryPredictor.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "Animation/AnimClassInterface.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "BoneControllers/AnimNode_Steering.h"
#include "Animation/AnimRootMotionProvider.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "Animation/Project_JAnimationQualityPolicy.h"

namespace
{
TAutoConsoleVariable<int32> PredictionRate(TEXT("ProjectJ.PlayerTrajectory.RemotePredictionHz"),60,
 TEXT("Mover presentation prediction for unprotected Mid/Far ground actors, 30..60 Hz. Local, air, attacks and sharp turns retain 60 Hz. Simulation rate is unchanged."));
const FStructProperty* PredictionRateProperty()
{
 static const auto* Property=FindFProperty<FStructProperty>(UMoverTrajectoryPredictor::StaticClass(),TEXT("MoverSamplingFrameRate"));
 return Property;
}
}

int32 UProject_JPlayerAnimInstance::GetTrajectoryPredictionRate() const
{
 const auto* Property=PredictionRateProperty();
 return MoverPredictor && Property ? Property->ContainerPtrToValuePtr<FFrameRate>(MoverPredictor.Get())->Numerator : 60;
}

void UProject_JPlayerAnimInstance::NativeInitializeAnimation()
{
 BindLocomotionSource(nullptr);
 // A retained anim instance must wait for this owner's first completed source
 // frame. Proxy reinitialization alone does not clear these graph admission flags.
 PlayerHasOwningActor = PlayerHasLocomotionSource = PlayerUseThreadSafeUpdateAnimation = false;
	Super::NativeInitializeAnimation();
}

void UProject_JPlayerAnimInstance::NativeUninitializeAnimation()
{
 BindLocomotionSource(nullptr);
 PlayerHasOwningActor = PlayerHasLocomotionSource = PlayerUseThreadSafeUpdateAnimation = false;
 Super::NativeUninitializeAnimation();
}

void UProject_JPlayerAnimInstance::BindLocomotionSource(UMoverComponent* Source)
{
 check(IsInGameThread());
 if (Source && LocomotionSource.Get()==Source) return;
 if (auto* Previous=LocomotionSource.Get()) Previous->OnBasedMovementApplied.RemoveAll(this);
 LocomotionSource=Source;
 // These values belong to one physical source, not to the retained AnimInstance.
 // Prediction, direction derivatives and air episode identity must change owners
 // together with the based-movement subscription.
 PendingBasedMovementDelta=FTransform::Identity;
 MoverPredictor=nullptr;MoverTrajectory.Samples.Reset();PredictorYaw=0.f;
 bNeedsLocomotionHistorySeed=true;PlayerHasLocomotionSource=false;
 bGroundRecoveryEligible=true;
 GroundPresentationSeconds=TNumericLimits<double>::Max();
 ActiveAirEpisode=0;bAirEntryPending=bAirLoop=false;AirSelectedAsset=nullptr;
 CommittedAirQuery={};AirEntryStartTime=0.f;LastAirReselection=-1.;LastAirBlendTime=.15f;
 AirReselectionCount=AirTransitionEntryCount=AirLoopEntryCount=0;
 LastAirReselectionFromTime=LastAirReselectionToTime=0.f;
 bWasPhysicallyInAir=false;LastLandVelocity=FVector::ZeroVector;
 if (Source) Source->OnBasedMovementApplied.AddUniqueDynamic(this,&ThisClass::AccumulateBasedMovement);
}

void UProject_JPlayerAnimInstance::AccumulateBasedMovement(const FTransform& Delta,const FMoverTimeStep&)
{
 check(IsInGameThread());
 // Preserve Mover's world-space translation / quaternion delta convention.
 PendingBasedMovementDelta.AddToTranslation(Delta.GetTranslation());
 PendingBasedMovementDelta.SetRotation((Delta.GetRotation()*PendingBasedMovementDelta.GetRotation()).GetNormalized());
}

void UProject_JPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	auto* Pawn = TryGetPawnOwner();
	auto* Mover = Project_J::Movement::FindMover(Pawn);
	if (!Mover)
	{
		BindLocomotionSource(nullptr);
		PlayerHasOwningActor = TryGetPawnOwner() != nullptr;
		PlayerUseThreadSafeUpdateAnimation = TryGetPawnOwner() != nullptr;
		PlayerHasLocomotionSource = false;
		Super::NativeUpdateAnimation(DeltaSeconds);
		return;
	}
	if (!Mover->HasBeenInitialized() || !Mover->HasBegunPlay())
	{
		BindLocomotionSource(nullptr);
		return;
	}
	UAnimInstance::NativeUpdateAnimation(DeltaSeconds);
	UpdateNativeConsoleVariables();
 BindLocomotionSource(Mover);
	if (NeedsOwnerReferenceRefresh()) CacheOwnerReferences();
	if (IsPrimaryMeshAnimInstance()) ApplyOptimizationPolicy(BuildOptimizationPolicy());
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;
	FillPlayerThreadSafeData(Data);
	FillMountThreadSafeData(Data);
	FillMontageThreadSafeData(Data);
	Data.bIsLocallyControlled = Pawn->IsLocallyControlled();
	Data.Movement.Velocity = Mover->GetVelocity();
	Data.Movement.GroundSpeed = Data.Movement.Velocity.Size2D();
	Data.Air.bIsInAir = Mover->IsAirborne();
	Data.Air.bIsFalling = Mover->IsFalling();
	Data.Movement.bHasGameplayRootMotion = Mover->HasFeaturesWithTag(Mover_AnimRootMotion_Montage,false);
	const auto* Input = Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const auto* Intent = Mover->GetLastInputCmd().InputCollection.FindDataByType<FProject_JMoverInputs>();
	const auto* PhysicalLife=Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
	const auto* ComparisonPawn = Cast<AProject_JMoverPawn>(Pawn);
	const bool bStrafe = PhysicalLife?PhysicalLife->bStrafe:Intent ? Intent->bStrafe : ComparisonPawn && ComparisonPawn->WantsStrafe();
	const bool bSprint = PhysicalLife?PhysicalLife->bSprint:Intent ? Intent->bSprint : ComparisonPawn && ComparisonPawn->WantsSprint();
	const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
	const FVector Move = Input ? Input->GetMoveInput_WorldSpace() : FVector::ZeroVector;
	Data.Input.bHasMoveInput = !Move.IsNearlyZero();
	Data.Input.MovementDirection = Move.IsNearlyZero()?0.f:FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw,Move.Rotation().Yaw);
	Data.Movement.Acceleration = Move.GetSafeNormal() * (Settings ? Settings->Acceleration : 4000.f);
	Data.Movement.RelativeVelocityDirection = FMath::UnwindDegrees(Data.Movement.Velocity.Rotation().Yaw - Pawn->GetActorRotation().Yaw);
	Data.LocomotionContext.DesiredFacingYaw = Input && !Input->GetOrientationIntentDir_WorldSpace().IsNearlyZero() ?
		Input->GetOrientationIntentDir_WorldSpace().Rotation().Yaw : Pawn->GetActorRotation().Yaw;
	if(bStrafe && Input) Data.LocomotionContext.DesiredFacingYaw=Input->ControlRotation.Yaw;
	Data.LocomotionContext.RotationMode = bStrafe ? EProject_JLocomotionRotationMode::Strafe : EProject_JLocomotionRotationMode::OrientToMovement;
	Data.LocomotionContext.GaitIntent = Intent && Intent->bWalk && !bSprint ? EProject_JLocomotionGaitIntent::Walk : EProject_JLocomotionGaitIntent::Run;
	Data.Ground.bWantsSprint = bSprint;
	if (const auto* ProjectMover=Cast<UProject_JMoverComponent>(Mover))
	{
		Data.Ground.RunSpeed=ProjectMover->GroundMovementSettings.RunSpeed;
		Data.Ground.SprintSpeed=ProjectMover->GroundMovementSettings.SprintSpeed;
	}
	if (const auto* Player = Cast<AProject_JPlayerCharacter>(Pawn))
	{
		if (const auto* Profile=Player->GetLocomotionProfile()) Data.Ground.bScalePlayRateWithSpeed=Profile->bScaleGroundPlayRateWithSpeed;
		if (const auto* State = Player->GetLocomotionAnimStateComponent())
		{
			Data.Air.bIsJumping = State->bIsJumping;
			Data.Air.bIsFallOffStart = State->bIsFallOffStart;

		}
	}
	if (const auto* Life=Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>()) Data.Air.bIsJumping=Life->bJumpEpisode && Data.Air.bIsInAir;
	if (!MoverPredictor)
	{
		MoverPredictor = NewObject<UMoverTrajectoryPredictor>(this);
		MoverPredictor->Setup(Mover);
	}
	TScriptInterface<IPoseSearchTrajectoryPredictorInterface> Predictor(MoverPredictor);
	const bool bSettledTranslation=(Move.IsNearlyZero() && Data.Movement.GroundSpeed<1.f) ||
		(!Move.IsNearlyZero() && Settings && FMath::Abs(Data.Movement.GroundSpeed-Settings->MaxSpeed)<1.f &&
		FVector::DotProduct(Move.GetSafeNormal2D(),Data.Movement.Velocity.GetSafeNormal2D())>.999f);
	const bool bSettledFacing=FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetActorRotation().Yaw,Data.LocomotionContext.DesiredFacingYaw))<1.f;
	const bool bBudgetEligible=bSettledTranslation && bSettledFacing && !ProjectJ::Animation::IsLocalPlayerAvatar(Pawn) && !Data.Air.bIsInAir &&
		!Data.Combat.bIsAttacking && !Data.Combat.bIsDodging && !Data.Combat.bIsHitReacting &&
		!IsAnyMontagePlaying() && !NeedsFullRateVisualUpdate() &&
		(CurrentOptimizationPolicy.Tier==EProject_JAnimBudgetTier::Mid || CurrentOptimizationPolicy.Tier==EProject_JAnimBudgetTier::Far);
	if (const auto* Property=PredictionRateProperty())
		*Property->ContainerPtrToValuePtr<FFrameRate>(MoverPredictor.Get())=FFrameRate(bBudgetEligible?FMath::Clamp(PredictionRate.GetValueOnGameThread(),30,60):60,1);
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectoryWithPredictor(Predictor, DeltaSeconds,
		MoverTrajectory, PredictorYaw, Data.Movement.Trajectory, .033f, 15, .1f, 15);
	Data.Movement.FutureTrajectoryVelocity = (Data.Movement.Trajectory.GetSampleAtTime(.6f).Position -
		Data.Movement.Trajectory.GetSampleAtTime(.4f).Position) / .2f;
	// Prediction is in visual-root space (Manny's mesh carries a -90 degree offset).
	// Compare two samples in that space; actor yaw made straight motion look urgent.
	static const auto* SourcePolicy=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.AnimationQuality.SourcePolicy.Enabled"));
	const float ReferenceYaw=SourcePolicy && SourcePolicy->GetInt()==0 ? Pawn->GetActorRotation().Yaw :
		Data.Movement.Trajectory.GetSampleAtTime(0.f).Facing.Rotator().Yaw;
	Data.Movement.FutureTrajectoryTurnAngle = FMath::FindDeltaAngleDegrees(ReferenceYaw,
		Data.Movement.Trajectory.GetSampleAtTime(.5f).Facing.Rotator().Yaw);
	Data.Movement.bHasTrajectory = !Data.Movement.Trajectory.Samples.IsEmpty();
	BuildProjectAirFrame(Data);
	ThreadSafeData = Data;
	PublishPlayerLocomotionSourceFrame(Data);
	GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().QueueGameThreadData(Data);
}

FString UProject_JPlayerAnimInstance::InspectFoundationPlayback() const
{
	FString Result;
	const auto* Mesh = GetSkelMeshComponent();
	if (Mesh && !Mesh->IsRunningParallelEvaluation())
	{
		const auto* Attribute = Mesh->GetCustomAttributes().Find<FTransformAnimationAttribute>(
			UE::Anim::FAttributeId(UE::Anim::IAnimRootMotionProvider::AttributeName, FCompactPoseBoneIndex(0)));
		Result += Attribute ? FString::Printf(TEXT("RootDelta=%s Yaw=%.6f; "),
			*Attribute->Value.GetTranslation().ToCompactString(), Attribute->Value.Rotator().Yaw) : TEXT("RootDelta=Missing; ");
	}
	const auto& Proxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	const auto& Air=Proxy.GetThreadSafeData().PlayerLocomotionAir;
	Result+=FString::Printf(TEXT("RecoveryEligible=%d ActionOwns=%d GroundPresentation=%.3f; "),bGroundRecoveryEligible,IsActionControllingLocomotion(),GroundPresentationSeconds==TNumericLimits<double>::Max()?-1.:GroundPresentationSeconds);
	Result+=FString::Printf(TEXT("AirEpisode=%llu FallOff=%d Jumping=%d LiveAngle=%.2f Gait=%d AirEntries=%d/%d QueryMode=%d QueryDir=%d AirAsset=%s; "),Air.Episode,Air.Query.bFallOff,Proxy.GetThreadSafeData().Air.bIsJumping,Air.LiveDirectionDegrees,int32(Air.Query.Gait),AirTransitionEntryCount,AirLoopEntryCount,int32(CommittedAirQuery.RotationMode),int32(CommittedAirQuery.Direction),*GetNameSafe(AirSelectedAsset));
	const auto* Interface = Proxy.GetAnimClassInterface();
	if (!Interface) return TEXT("No generated animation class");
	for (int32 Index = 0; Index < Interface->GetAnimNodeProperties().Num(); ++Index)
	{
		const auto* Node = Proxy.GetNodeFromIndex<FAnimNode_BlendStack_Standalone>(Index);
		if (!Node) continue;
		Result += FString::Printf(TEXT("Index=%d Asset=%s Time=%.3f Length=%.3f Weight=%.3f Players=%d Rate=%.3f; "),
			Index, *GetPathNameSafe(Node->GetAnimAsset()), Node->GetCurrentAssetTime(), Node->GetCurrentAssetLength(),
			Node->GetCachedBlendWeight(), Node->AnimPlayers.Num(), Node->AnimPlayers.IsEmpty() ? 0.f : Node->AnimPlayers[0].GetPlayRate());
	}
	for (int32 Index = 0; Index < Interface->GetAnimNodeProperties().Num(); ++Index)
	{
		if (const auto* Steering = Proxy.GetNodeFromIndex<FAnimNode_Steering>(Index))
			Result += FString::Printf(TEXT("Steering=%d Alpha=%.3f Actual=%.3f Enabled=%d Target=%.3f Asset=%s SpeedGate=%.3f; "),
				Index, Steering->Alpha, Steering->ActualAlpha, Steering->bAlphaBoolEnabled, Steering->TargetOrientation.Rotator().Yaw,
				*GetNameSafe(Steering->CurrentAnimAsset), Steering->DisableSteeringBelowSpeed);
	}
	return Result;
}

FTransformTrajectory UProject_JPlayerAnimInstance::GetFoundationTrajectory() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Trajectory;
}

void UProject_JPlayerAnimInstance::PublishPlayerLocomotionSourceFrame(const FProject_JAnimThreadSafeData& Data)
{
	auto* Pawn = TryGetPawnOwner();
	if (!Pawn) return;
	const auto* Mover = Project_J::Movement::FindMover(Pawn);
	const auto* Settings = Mover ? Mover->FindSharedSettings<UCommonLegacyMovementSettings>() : nullptr;
	if (!Mover || !Settings) return;
	if (bWasPhysicallyInAir && !Data.Air.bIsInAir) LastLandVelocity = CastChecked<AProject_JMoverPawn>(Pawn)->GetLastLandingVelocity();
 PlayerCharacterProperties.ActorTransform = Pawn->GetActorTransform();
 PlayerCharacterProperties.Velocity = Data.Movement.Velocity;
 const auto* Command=Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>();
 PlayerCharacterProperties.InputAcceleration=Command?Command->GetMoveInput_WorldSpace():FVector::ZeroVector;
 // Preserve the sample's zero-valued legacy acceleration fields.
 PlayerCharacterProperties.CurrentMaxAcceleration=0.;
 PlayerCharacterProperties.OrientationIntent=FRotator(0.f,Data.LocomotionContext.DesiredFacingYaw,0.f);
 PlayerCharacterProperties.AimingRotation=Command?Command->ControlRotation:Pawn->GetBaseAimRotation();
 PlayerCharacterProperties.LandVelocity=LastLandVelocity;
 FHitResult Floor;Mover->TryGetFloorCheckHitResult(Floor);
 PlayerCharacterProperties.GroundNormal=Floor.bBlockingHit?Floor.ImpactNormal:FVector::UpVector;
 PlayerCharacterProperties.BasedMovementDelta=PendingBasedMovementDelta;
 PendingBasedMovementDelta=FTransform::Identity;
 PlayerCharacterProperties.MovementMode=Data.Air.bIsInAir?EProject_JPlayerMovementMode::InAir:EProject_JPlayerMovementMode::OnGround;
 PlayerCharacterProperties.Stance=EProject_JPlayerStance::Stand;
 const bool bStrafe=Data.LocomotionContext.RotationMode==EProject_JLocomotionRotationMode::Strafe;
 PlayerCharacterProperties.RotationMode=bStrafe?EProject_JPlayerRotationMode::Strafe:EProject_JPlayerRotationMode::OrientToMovement;
 const auto* Intent=Mover->GetLastInputCmd().InputCollection.FindDataByType<FProject_JMoverInputs>();
 const uint8 Direction=bStrafe && !Data.Ground.bWantsSprint && Intent?FMath::Min<uint8>(Intent->MovementDirection,5):0;
 PlayerCharacterProperties.MovementDirection=static_cast<EProject_JPlayerMovementDirection>(Direction);
 PlayerCharacterProperties.Gait=Data.Ground.bWantsSprint?EProject_JLocomotionGaitIntent::Sprint:Data.LocomotionContext.GaitIntent==EProject_JLocomotionGaitIntent::Walk?EProject_JLocomotionGaitIntent::Walk:EProject_JLocomotionGaitIntent::Run;
 PlayerCharacterProperties.InputState.WantsToSprint=Data.Ground.bWantsSprint;
 PlayerCharacterProperties.InputState.WantsToStrafe=bStrafe;
 PlayerHasLocomotionSource=PlayerHasOwningActor=PlayerUseThreadSafeUpdateAnimation=true;
	bWasPhysicallyInAir = Data.Air.bIsInAir;
}


FName UProject_JPlayerAnimInstance::GetFoundationStateName() const
{
 check(IsInGameThread());
 return PlayerStateName;
}
