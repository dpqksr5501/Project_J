#include "Movement/Project_JMoverComponent.h"
#include "Components/SceneComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMoverPolicy.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "MoveLibrary/MovementUtils.h"
#include "DefaultMovementSet/Modes/SimpleWalkingMode.h"

UProject_JFallingMode::UProject_JFallingMode(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
 // Let go of direction without braking the jump's horizontal momentum.
 FallingDeceleration=0.f;
}

void UProject_JFallingMode::GenerateMove_Implementation(const FMoverSimContext& SimContext,const FMoverTickStartData& Start,const FMoverTimeStep& Step,FProposedMove& Move) const
{
 // Retain the engine's gravity, terminal fall speed, planar/wall constraints and
 // collision/landing simulation. Air control scales acceleration, not gait speed.
 Super::GenerateMove_Implementation(SimContext,Start,Step,Move);
 const auto* Input=Start.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
 const auto* Sync=Start.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
 const auto* Intent=Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
 if (!Input || !Sync) return;
 const auto* Mover=GetMoverComponent();
 const FVector Up=Mover->GetUpDirection();
 const float Dt=Step.StepMs*.001f;
 if (Input->GetMoveInputType()==EMoveInputType::DirectionalIntent)
 {
  FComputeVelocityParams Params;
  Params.InitialVelocity=FVector::VectorPlaneProject(Sync->GetVelocity_WorldSpace(),Up);
  // Super has already constrained and filtered this direction against the wall.
  Params.MoveDirectionIntent=AirControlPercentage>UE_SMALL_NUMBER?Move.DirectionIntent/AirControlPercentage:FVector::ZeroVector;
  Params.MoveInput=Params.MoveDirectionIntent;
  Params.MoveInputType=EMoveInputType::DirectionalIntent;
  Params.DeltaSeconds=Dt;
  Params.MaxSpeed=CommonLegacySettings->MaxSpeed;
  Params.Acceleration=CommonLegacySettings->Acceleration*AirControlPercentage;
  Params.Deceleration=Params.InitialVelocity.Size()>=TerminalMovementPlaneSpeed?OverTerminalSpeedFallingDeceleration:FallingDeceleration;
  Params.Friction=FallingLateralFriction;
  Params.TurningBoost=CommonLegacySettings->TurningBoost;
  const FVector Horizontal=UMovementUtils::ComputeVelocity(Params);
  Move.LinearVelocity=Horizontal+Up*FVector::DotProduct(Move.LinearVelocity,Up);
  Move.DirectionIntent=Params.MoveDirectionIntent;
  Move.bHasDirIntent=!Move.DirectionIntent.IsNearlyZero();
 }
 // GASP BP_MovementMode_Falling applies the authored strafe offset as well as
 // orientation intent. Use the recorded command and step, never world delta time.
 FRotator Facing=Input->OrientationIntent.IsNearlyZero()?Sync->GetOrientation_WorldSpace():Input->GetOrientationIntentDir_WorldSpace().Rotation();
 if (Intent && Intent->bStrafe) Facing.Yaw+=Intent->RotationOffset;
 Facing=UMovementUtils::ApplyGravityToOrientationIntent(Facing,Mover->GetWorldToGravityTransform(),CommonLegacySettings->bShouldRemainVertical);
 Move.AngularVelocityDegrees=UMovementUtils::ComputeAngularVelocityDegrees(Sync->GetOrientation_WorldSpace(),Facing,Dt,AirTurningRate);
}

namespace
{
FProject_JMoverPolicy ApplyGameplayPolicy(UMoverComponent* Mover,const FMoverTickStartData& Start)
{
 auto* Intent=Start.InputCmd.InputCollection.FindMutableDataByType<FProject_JMoverInputs>();
 // Local producers and trajectory prediction can bypass transport serialization.
 // Bound the working copy once before any Walking/Falling consumer reads it.
 if (Intent)
 {
  Intent->RotationOffset=FMath::IsFinite(Intent->RotationOffset)?FMath::Clamp(Intent->RotationOffset,-180.f,180.f):0.f;
  if (!FMath::IsFinite(Intent->ControlRotationRate)) Intent->ControlRotationRate=0.f;
  if (Intent->MovementDirection>5) Intent->MovementDirection=0;
 }
 const FProject_JMoverInputs Request=Intent?*Intent:FProject_JMoverInputs();
 FProject_JMoverPolicy Policy;
 if (const auto* Source=Cast<IProject_JMoverPolicySource>(Mover->GetOwner())) Policy=Source->ResolveMoverPolicy(Request,Start.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>());
 else
 {
  const auto* Settings=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
  Policy.MaxSpeed=Settings?Settings->MaxSpeed:500.f;Policy.bStrafe=Request.bStrafe;Policy.bSprint=Request.bSprint;Policy.bAllowJump=true;
 }
 Policy.MaxSpeed=FMath::IsFinite(Policy.MaxSpeed)?FMath::Max(0.f,Policy.MaxSpeed):0.f;
 // Apply the approved gait to the solver's speed limit. Converting direction
 // commands into target velocities skips braking and clamps jump momentum to
 // AirControlPercentage * requested speed on the first falling frame.
 if (auto* Settings=Mover->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>()) Settings->MaxSpeed=Policy.MaxSpeed;
 if (Intent){Intent->bStrafe=Policy.bStrafe;Intent->bSprint=Policy.bSprint;}
 if (auto* Input=Start.InputCmd.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>())
 {
  const float MaxSpeed=FMath::Max(0.f,Policy.MaxSpeed);
  const FVector Requested=Input->GetMoveInput().ContainsNaN()?FVector::ZeroVector:Input->GetMoveInput();
  if (Input->GetMoveInputType()==EMoveInputType::Velocity) Input->SetMoveInput(EMoveInputType::Velocity,Requested.GetClampedToMaxSize(MaxSpeed));
  else if(Input->GetMoveInputType()==EMoveInputType::DirectionalIntent) Input->SetMoveInput(EMoveInputType::DirectionalIntent,MaxSpeed>0.f?Requested.GetClampedToMaxSize(1.f):FVector::ZeroVector);
  // Gameplay mode changes are queued by their owning action, never requested by a client command.
  Input->SuggestedMovementMode=NAME_None;
  if (Policy.bStrafe && (!Input->GetMoveInput().IsNearlyZero() || Start.SyncState.MovementMode==DefaultModeNames::Falling))
  {
   FVector Facing=FRotator(0.,Input->ControlRotation.Yaw,0.).Vector();
   if (Input->bUsingMovementBase && Input->MovementBase) UBasedMovementUtils::TransformWorldDirectionToBased(Input->MovementBase,Input->MovementBaseBoneName,Facing,Facing);
   Input->OrientationIntent=Facing;
  }
  Input->bIsJumpPressed &=Policy.bAllowJump;Input->bIsJumpJustPressed &=Policy.bAllowJump;
 }
 return Policy;
}
void StoreGameplayPolicy(FMoverTickEndData& Output,const FProject_JMoverPolicy& Policy)
{
 auto& Life=Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
 Life.bStrafe=Policy.bStrafe;Life.bSprint=Policy.bSprint;
}
}

void UProject_JMoverComponent::OnPreSimulate(const FMoverTimeStep& Step,const FMoverTickStartData& Start)
{
 check(IsInGameThread());
 // The installed NP liaison deep-copies its historical command into Start.
 // Mover's mutable data access updates only that working copy, before jump,
 // suggested-mode and movement consumers run. Recorded prediction input stays intact.
 ApplyGameplayPolicy(this,Start);
 // SmoothWalking is Blueprint-extensible in the installed engine. Keep the
 // sample's actual Blueprint class and cap the speed before its solver reads it.
 if(auto* Walking=Cast<USimpleWalkingMode>(MovementModes.FindRef(DefaultModeNames::Walking)))
  if(const auto* Settings=FindSharedSettings<UCommonLegacyMovementSettings>())
   if(Walking->MaxSpeedOverride>=0.f) Walking->MaxSpeedOverride=FMath::Min(Walking->MaxSpeedOverride,Settings->MaxSpeed);
 Super::OnPreSimulate(Step,Start);
}
void UProject_JMoverComponent::OnPostSimulate(const FMoverTimeStep& Step,const FMoverTickStartData& Start,FMoverTickEndData& End)
{
 FProject_JMoverPolicy Accepted;
 if(const auto* Intent=Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>()) {Accepted.bStrafe=Intent->bStrafe;Accepted.bSprint=Intent->bSprint;}
 StoreGameplayPolicy(End,Accepted);
 auto& Life=End.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
 const auto* Previous=Start.SyncState.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
 Life.AdvanceLandingBraking(Previous,Step.StepMs);
 if(End.SyncState.MovementMode==DefaultModeNames::Walking)
  End.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>().bJumpEpisode=false;
 Super::OnPostSimulate(Step,Start,End);
}

void UProject_JFallingMode::Activate(const FMoverEventContext& Context,FName Previous,const FMoverSimContext& SimContext,const FMoverTickStartData& Start,FMoverSyncState* Output,FMoverAuxStateContext* Aux)
{
 Super::Activate(Context,Previous,SimContext,Start,Output,Aux);
 if (Output)
 {
  auto& Life=Output->SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
  if (!Life.bJumpEpisode && Previous!=DefaultModeNames::Falling)
  {
   uint32 Stamp=uint32(uint64(FMath::Max(0.,Context.EventTimeMs)));if(!Stamp)Stamp=1;
   Life.AirSequence=uint64(Stamp)<<1;
  }
 }
}

void UProject_JFallingMode::ProcessLanded(const FFloorCheckResult& Floor,FVector Location,FVector& Velocity,FRelativeBaseInfo& Base,FMoverTickEndData& Output) const
{
 const FVector ImpactVelocity=Velocity;
 Super::ProcessLanded(Floor,Location,Velocity,Base,Output);
 if (!Output.MovementEndState.NextModeName.IsNone())
 {
  auto& Life=Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
  Life.LandingSequence=Life.AirSequence; Life.bJumpEpisode=false; Life.LandingVelocity=ImpactVelocity;
  LastLandingHit=Floor.HitResult;
 }
}

void UProject_JAttachedMovementMode::GenerateMove_Implementation(const FMoverSimContext&, const FMoverTickStartData&, const FMoverTimeStep&, FProposedMove& Move) const
{
	Move = FProposedMove();
}

void UProject_JAttachedMovementMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output)
{
	Output.SyncState = Params.StartState.SyncState;
	auto& State = Output.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	if (const auto* Updated = Params.MovingComps.UpdatedComponent.Get())
		State.SetTransforms_WorldSpace(Updated->GetComponentLocation(), Updated->GetComponentRotation(), FVector::ZeroVector, FVector::ZeroVector);
	Output.MovementEndState.RemainingMs = 0.f;
}

UProject_JMoverComponent::UProject_JMoverComponent()
{
 GroundWalkingMode=FSoftClassPath(TEXT("/Game/Animation_Logic/PlayerMovement/BP_MovementMode_Walking.BP_MovementMode_Walking_C"));
	MovementModes.Add(DefaultModeNames::Falling,CreateDefaultSubobject<UProject_JFallingMode>(TEXT("ProjectFallingMode")));
	PersistentSyncStateDataTypes.Add(FMoverDataPersistence(FProject_JMoverLifecycle::StaticStruct(),true));
	MovementModes.Add(TEXT("Attached"), CreateDefaultSubobject<UProject_JAttachedMovementMode>(TEXT("AttachedMovementMode")));
	bSyncInputsForSimProxy = true;
	SetHandleStanceChanges(false);
}

void UProject_JMoverComponent::OnRegister()
{
 if(!IsTemplate())
 {
  if(UClass* Class=GroundWalkingMode.LoadSynchronous())
  {
   auto* Current=MovementModes.FindRef(DefaultModeNames::Walking).Get();
   if(!Current || Current->GetClass()!=Class)
    MovementModes.Add(DefaultModeNames::Walking,NewObject<UBaseMovementMode>(this,Class));
  }
  else ensureMsgf(false,TEXT("GASP ground walking Blueprint is required"));
 }
 Super::OnRegister();
}

void UProject_JMoverComponent::PostInitProperties()
{
	Super::PostInitProperties();
	// Native default subobjects are validated before OnRegister/PostCDOCompiled.
	RefreshSharedSettings();
}

void UProject_JMoverComponent::BeginPlay()
{
	Super::BeginPlay();
	OnPostFinalize.AddDynamic(this, &ThisClass::FinishExternalMovement);
}

void UProject_JMoverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 StopGameplayMontageRootMotion();
 OnPostFinalize.RemoveAll(this);
 Super::EndPlay(Reason);
}

void UProject_JMoverComponent::SetGameplayMovementMode(EMovementMode Mode)
{
	if (Mode!=MOVE_None && !bExternalMovementSession)
	{
		QueueNextMode(Mode==MOVE_Flying?DefaultModeNames::Flying:Mode==MOVE_Walking || Mode==MOVE_NavWalking?DefaultModeNames::Walking:DefaultModeNames::Falling);
		return;
	}
	if (!bExternalMovementSession)
	{
		bSavedExternalWarning = bWarnOnExternalMovement;
		bExternalMovementSession = true;
	}
	bWarnOnExternalMovement = false;
	if (Mode == MOVE_None)
	{
		PendingRestoreMode = NAME_None;
		bAcceptExternalMovement = true;
		QueueNextMode(TEXT("Attached"));
	}
	else
	{
		PendingRestoreMode = Mode == MOVE_Flying ? DefaultModeNames::Flying : Mode == MOVE_Walking || Mode == MOVE_NavWalking ? DefaultModeNames::Walking : DefaultModeNames::Falling;
		// The attachment owner may just have teleported this pawn to a dismount location.
		bAcceptExternalMovement = true;
		QueueNextMode(PendingRestoreMode);
	}
}

void UProject_JMoverComponent::FinishExternalMovement(const FMoverSyncState& State, const FMoverAuxStateContext&)
{
	if (!PendingRestoreMode.IsNone() && State.MovementMode == PendingRestoreMode)
	{
		PendingRestoreMode = NAME_None;
		bAcceptExternalMovement = false;
		bWarnOnExternalMovement = bSavedExternalWarning;
		bExternalMovementSession = false;
	}
}


bool FProject_JJumpImpulseEffect::ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& Output)
{
 if (!FJumpImpulseEffect::ApplyMovementEffect(Params,Output)) return false;
 auto& Life=Output.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
 const auto* Request=Params.StartState->InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
 Life.JumpSequence=Request && Request->JumpRequestId?Request->JumpRequestId:Life.JumpSequence+1;
 Life.AirSequence=(uint64(Life.JumpSequence)<<1)|1;Life.bJumpEpisode=true;
 return true;
}

bool UProject_JMoverComponent::Jump()
{
 if (!CanActorJump()) return false;
 if (const auto* Source=Cast<IProject_JMoverPolicySource>(GetOwner()))
 {
  FProject_JMoverInputs Request;
  if (!Source->ResolveMoverPolicy(Request).bAllowJump) return false;
 }
 if (const auto* Settings=FindSharedSettings<UCommonLegacyMovementSettings>())
 {
  auto Effect=MakeShared<FProject_JJumpImpulseEffect>();Effect->UpwardsSpeed=Settings->JumpUpwardsSpeed;
  QueueInstantMovementEffect(Effect);return true;
 }
 return false;
}

bool FProject_JFacingEffect::ApplyMovementEffect(FApplyMovementEffectParams&, FMoverSyncState& Output)
{
 auto* State=Output.SyncStateCollection.FindMutableDataByType<FMoverDefaultSyncState>();
 if (!State) return false;
 State->SetTransforms_WorldSpace(State->GetLocation_WorldSpace(),Facing,State->GetVelocity_WorldSpace(),FVector::ZeroVector,State->GetMovementBase(),State->GetMovementBaseBoneName());
 return true;
}

void UProject_JMoverComponent::QueueGameplayFacing(FRotator Facing)
{
 auto Effect=MakeShared<FProject_JFacingEffect>(); Effect->Facing=Facing;
 QueueInstantMovementEffect(Effect);
}

void UProject_JMoverComponent::StopGameplayMontageRootMotion(UAnimMontage* ExpectedMontage)
{
 if (ExpectedMontage && RootMotionMontage!=ExpectedMontage) return;
 CancelFeaturesWithTag(Mover_AnimRootMotion_Montage,false);
 if(auto* Anim=RootMotionAnim.Get())
 {
  Anim->OnMontageSectionChanged.RemoveAll(this);
  Anim->OnMontageEnded.RemoveAll(this);
  if(auto* Instance=Anim->GetMontageInstanceForID(RootMotionMontageInstance)) Instance->PopDisableRootMotion();
 }
 RootMotionAnim.Reset();
 RootMotionMontage.Reset(); RootMotionMontageInstance=INDEX_NONE;
}

void UProject_JMoverComponent::GameplayMontageSectionChanged(UAnimMontage* Montage,FName,bool)
{
 if (RootMotionMontage!=Montage) return;
 auto* Anim=RootMotionAnim.Get();
 const auto* Instance=Anim?Anim->GetActiveInstanceForMontage(Montage):nullptr;
 if (Instance && Instance->GetInstanceID()==RootMotionMontageInstance) FollowGameplayMontage(Montage);
}

void UProject_JMoverComponent::GameplayMontageEnded(UAnimMontage* Montage,bool)
{
 if(RootMotionMontage!=Montage) return;
 // An older instance of the same asset can finish after its replacement starts.
 // Its queued multicast event must not cancel the replacement's movement.
 if(auto* Anim=RootMotionAnim.Get())
  if(const auto* Current=Anim->GetMontageInstanceForID(RootMotionMontageInstance);Current && Current->IsPlaying()) return;
 StopGameplayMontageRootMotion(Montage);
}

void UProject_JMoverComponent::FollowGameplayMontage(UAnimMontage* Montage)
{
 // Simulated proxies consume replicated Mover state, rather than creating a second move from GAS playback.
 if (!GetOwner() || GetOwner()->GetLocalRole()==ROLE_SimulatedProxy) return;
 // This avatar uses the Network Prediction backend. The engine's async montage
 // struct has an unexported OnStart_Async and cannot be instantiated by a game module.
 if (!ensureMsgf(!IsBackendAsync(),TEXT("Gameplay montage extraction requires the configured game-thread Mover backend"))) return;
 auto* Mesh=Cast<USkeletalMeshComponent>(GetPrimaryVisualComponent());
 auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
 auto* Instance=Anim && Montage?Anim->GetActiveInstanceForMontage(Montage):nullptr;
 if (!Instance || !Montage->HasRootMotion() || FMath::IsNearlyZero(Instance->GetPlayRate()) || FMath::IsNearlyZero(Montage->RateScale))
 { StopGameplayMontageRootMotion(); return; }
 const bool bSameInstance=RootMotionMontage==Montage && RootMotionMontageInstance==Instance->GetInstanceID();
 if (!bSameInstance)
 {
  StopGameplayMontageRootMotion();
  RootMotionAnim=Anim;
  Anim->OnMontageSectionChanged.AddUniqueDynamic(this,&ThisClass::GameplayMontageSectionChanged);
  Anim->OnMontageEnded.AddUniqueDynamic(this,&ThisClass::GameplayMontageEnded);
  Instance->PushDisableRootMotion();
 }
 else CancelFeaturesWithTag(Mover_AnimRootMotion_Montage,false);
 RootMotionMontage=Montage; RootMotionMontageInstance=Instance->GetInstanceID();
 const float Start=Instance->GetPosition(), Rate=Instance->GetPlayRate();
 TSharedPtr<FLayeredMove_AnimRootMotion> Move=MakeShared<FLayeredMove_AnimRootMotion>();
 Move->MontageState.Montage=Montage;
 Move->MontageState.PlayRate=Rate;
 Move->MontageState.StartingMontagePosition=Start;
 Move->MontageState.CurrentPosition=Start;
 Move->MontageState.BlendOutTimeSeconds=Montage->GetDefaultBlendOutTime();
 Move->MontageState.bEnableAutoBlendOut=Montage->CompositeSections.IsEmpty() || Montage->CompositeSections.Last().NextSectionName==NAME_None;
 Move->DurationMs=(Rate>0.f?Montage->GetPlayLength()-Start:Start)/FMath::Abs(Rate*Montage->RateScale)*1000.f;
 Move->MixMode=EMoveMixMode::OverrideAll;
 QueueLayeredMove(Move);
}

bool UProject_JMoverComponent::ApplyAuthorityHandoverTransform(const FTransform& Transform)
{
 auto* Owner=GetOwner();
 if (!Owner || !Owner->HasAuthority() || Owner->IsActorBeingDestroyed() || Transform.ContainsNaN()) return false;
 FMoverSyncState Before, Updated;
 const bool bRunning=HasBegunPlay();
 if (bRunning)
 {
  if (!BackendLiaisonComp || !BackendLiaisonComp->ReadPendingSyncState(Before)) return false;
  Updated=Before;
  auto& Physical=Updated.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
  Physical.SetTransforms_WorldSpace(Transform.GetLocation(),Transform.Rotator(),FVector::ZeroVector,FVector::ZeroVector);
  if (!BackendLiaisonComp->WritePendingSyncState(Updated)) return false;
 }
 if (!Owner->SetActorLocationAndRotation(Transform.GetLocation(),Transform.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics))
 {
  if (bRunning) BackendLiaisonComp->WritePendingSyncState(Before);
  return false;
 }
 if (bRunning && !Owner->IsActorBeingDestroyed())
 {
  BackendLiaisonComp->WritePresentationSyncState(Updated);
  BackendLiaisonComp->WritePrevPresentationSyncState(Updated);
  SetFrameStateFromContext(&Updated,&CachedLastAuxState,false);
 }
 if (auto* UpdatedScene=GetUpdatedComponent()) UpdatedScene->ComponentVelocity=FVector::ZeroVector;
 return !Owner->IsActorBeingDestroyed();
}
