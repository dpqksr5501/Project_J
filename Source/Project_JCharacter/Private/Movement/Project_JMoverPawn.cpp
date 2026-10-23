#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "Movement/Project_JMoverInputs.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "GameFramework/Controller.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JPlayerLocomotionTypes.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Chooser.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
const FName AProject_JMoverPawn::MeshComponentName(TEXT("CharacterMesh0"));
const FProject_JStrafeDirectionPolicy& AProject_JMoverPawn::GetStrafeDirectionPolicy() const
{
 static const FProject_JStrafeDirectionPolicy Default;
 return Default;
}
AProject_JMoverPawn::AProject_JMoverPawn(const FObjectInitializer& OI):Super(OI)
{
 bReplicates=true; SetReplicatingMovement(false); bUseControllerRotationYaw=false;
 Capsule=CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
 Capsule->InitCapsuleSize(34.f,90.f); Capsule->SetCollisionProfileName(TEXT("Pawn")); SetRootComponent(Capsule);
 Mesh=CreateDefaultSubobject<USkeletalMeshComponent>(MeshComponentName); Mesh->SetupAttachment(Capsule);
 Mesh->ComponentTags.AddUnique(TEXT("ProjectJ.PoseSource"));
 Mesh->SetRelativeLocation(FVector(0,0,-90)); Mesh->SetRelativeRotation(FRotator(0,-90,0)); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Mover=CreateDefaultSubobject<UProject_JMoverComponent>(TEXT("CharacterMover")); Mover->bGatherInputFromAllInputProducerComponents=false; Mover->bSyncInputsForSimProxy=true; Mover->SetHandleStanceChanges(false);
 NavMover=CreateDefaultSubobject<UNavMoverComponent>(TEXT("NavMover"));
 RotationOffsetChooser=LoadObject<UChooserTable>(nullptr,TEXT("/Game/Animation_Logic/PlayerMovement/CHT_RotationOffsetCurve.CHT_RotationOffsetCurve"));
}
void AProject_JMoverPawn::PostInitializeComponents()
{
 Super::PostInitializeComponents(); Mover->InputProducer=this; Mover->SetUpdatedComponent(Capsule); Mover->SetPrimaryVisualComponent(Mesh);
 Mesh->ComponentTags.AddUnique(TEXT("ProjectJ.PoseSource")); Mesh->AddTickPrerequisiteComponent(Mover); Capsule->SetCanEverAffectNavigation(bCanAffectNavigationGeneration);
 if(auto* Settings=Mover->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>())
 { Settings->MaxSpeed=800.f; Settings->Acceleration=Settings->Deceleration=4000.f; Settings->TurningRate=500.f; Settings->JumpUpwardsSpeed=500.f; }
}
void AProject_JMoverPawn::BeginPlay()
{
 Super::BeginPlay(); Mover->OnPostFinalize.AddDynamic(this,&ThisClass::FinalizePhysicalLifecycle);
 Mover->OnBasedMovementApplied.AddUniqueDynamic(this,&ThisClass::UpdateBasedOrientation);
 LastOrientation=GetActorForwardVector();
}
void AProject_JMoverPawn::EndPlay(const EEndPlayReason::Type Reason)
{
 Mover->OnPostFinalize.RemoveAll(this); Mover->OnBasedMovementApplied.RemoveAll(this);
 bJumpPressed=bJumpJustPressed=bJumpEpisode=false; Super::EndPlay(Reason);
}
void AProject_JMoverPawn::UpdateBasedOrientation(const FTransform& Delta,const FMoverTimeStep&)
{
 const auto* Settings=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
 if (Settings && Settings->bIgnoreBaseRotation) return;
 // Both modes retain their previous orientation on a rotating base while idle.
 LastOrientation=FVector::VectorPlaneProject(Delta.GetRotation().RotateVector(LastOrientation),Mover->GetUpDirection()).GetSafeNormal();
}
FVector AProject_JMoverPawn::GetVelocity() const { return Mover?Mover->GetVelocity():FVector::ZeroVector; }
FVector AProject_JMoverPawn::GetNavAgentLocation() const { return NavMover?NavMover->GetFeetLocation():GetActorLocation(); }
void AProject_JMoverPawn::UpdateNavigationRelevance() { if(Capsule) Capsule->SetCanEverAffectNavigation(bCanAffectNavigationGeneration); }
FVector AProject_JMoverPawn::GetBaseTranslationOffset() const { return Mesh->GetRelativeLocation(); }
FQuat AProject_JMoverPawn::GetBaseRotationOffset() const { return Mesh->GetRelativeRotation().Quaternion(); }
void AProject_JMoverPawn::Jump() { SetJumpPressed(true); }
void AProject_JMoverPawn::StopJumping() { SetJumpPressed(false); }
void AProject_JMoverPawn::SetJumpPressed(bool Value)
{
 if (Value && !bJumpPressed)
 {
  if (const auto* Life=Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>())
   if(int32(Life->JumpSequence-JumpRequestId)>0) JumpRequestId=Life->JumpSequence;
  if(++JumpRequestId==0) ++JumpRequestId;
  bJumpJustPressed=true;
 }
 bJumpPressed=Value;
}
bool AProject_JMoverPawn::CanJumpInternal_Implementation() const { return Mover&&Mover->CanActorJump(); }
void AProject_JMoverPawn::OnJumped_Implementation() {}
void AProject_JMoverPawn::Landed(const FHitResult& Hit) { K2_OnLanded(Hit); }
void AProject_JMoverPawn::FinalizePhysicalLifecycle(const FMoverSyncState& State,const FMoverAuxStateContext&)
{
 const auto* Life=State.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
 if (!Life) return;
 bJumpEpisode=Life->bJumpEpisode;
 const bool bGameplayRecipient=HasAuthority() || IsLocallyControlled();
 if (int32(Life->JumpSequence-ConsumedJumpSequence)>0)
 {
  ConsumedJumpSequence=Life->JumpSequence;
  if (bGameplayRecipient) OnJumped();
 }
 if (IsActorBeingDestroyed()) return;
 const bool bJumpLanding=(Life->LandingSequence&1)!=0;
 const uint32 LandingId=uint32(Life->LandingSequence>>1);
 uint32& ConsumedLanding=bJumpLanding?ConsumedJumpLanding:ConsumedFallLanding;
 if (Life->LandingSequence && int32(LandingId-ConsumedLanding)>0)
 {
  ConsumedLanding=LandingId;
  LastLandingVelocity=Life->LandingVelocity;
  if (bGameplayRecipient)
   if (const auto* Mode=Cast<UProject_JFallingMode>(Mover->MovementModes.FindRef(DefaultModeNames::Falling))) Landed(Mode->GetLastLandingHit());
 }
}

FString AProject_JMoverPawn::InspectPhysicalLifecycle() const
{
 const auto* Life=Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
 return Life?FString::Printf(TEXT("Air=%llu Jump=%u Land=%llu JumpEpisode=%d Accepted=%d/%d Consumed=%u/%u/%u Impact=%s"),Life->AirSequence,Life->JumpSequence,Life->LandingSequence,Life->bJumpEpisode,Life->bStrafe,Life->bSprint,ConsumedJumpSequence,ConsumedJumpLanding,ConsumedFallLanding,*FVector(Life->LandingVelocity).ToCompactString()):TEXT("No physical lifecycle");
}

float AProject_JMoverPawn::GetMoverMaxSpeed(bool) const
{
 const auto* Settings=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
 const float Speed=Settings?Settings->MaxSpeed:500.f;
 return FMath::IsFinite(Speed) && Speed>=0.f?Speed:0.f;
}

FProject_JMoverPolicy AProject_JMoverPawn::ResolveMoverPolicy(const FProject_JMoverInputs& Requested,const FCharacterDefaultInputs*) const
{
 FProject_JMoverPolicy Policy;
 Policy.MaxSpeed=GetMoverMaxSpeed(Requested.bSprint);
 Policy.bStrafe=Requested.bStrafe;Policy.bSprint=Requested.bSprint;Policy.bAllowJump=true;
 return Policy;
}

void AProject_JMoverPawn::FaceGameplayDirection(FRotator Facing)
{
 LastOrientation=Facing.Vector();
 CastChecked<UProject_JMoverComponent>(Mover)->QueueGameplayFacing(Facing);
}
float AProject_JMoverPawn::PlayAnimMontage(UAnimMontage* Montage,float Rate,FName StartSection)
{
 auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr; if(!Anim||!Montage) return 0.f;
 const float Duration=Anim->Montage_Play(Montage,Rate);
 if(Duration>0.f)
 {
  if(!StartSection.IsNone()) Anim->Montage_JumpToSection(StartSection,Montage);
  CastChecked<UProject_JMoverComponent>(Mover)->FollowGameplayMontage(Montage);
 }
 return Duration;
}
void AProject_JMoverPawn::StopAnimMontage(UAnimMontage* Montage)
{
 if(auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr) if(auto* Active=Montage?Montage:Anim->GetCurrentActiveMontage())
 {
  Anim->Montage_Stop(Active->BlendOut.GetBlendTime(),Active);
  CastChecked<UProject_JMoverComponent>(Mover)->StopGameplayMontageRootMotion(Active);
 }
}
UAnimMontage* AProject_JMoverPawn::GetCurrentMontage() const { auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr; return Anim?Anim->GetCurrentActiveMontage():nullptr; }
void AProject_JMoverPawn::LaunchCharacter(FVector Velocity,bool bXYOverride,bool bZOverride)
{
 const FVector Current=GetVelocity(); if(!bXYOverride){Velocity.X+=Current.X;Velocity.Y+=Current.Y;} if(!bZOverride) Velocity.Z+=Current.Z;
 auto Effect=MakeShared<FApplyVelocityEffect>(); Effect->VelocityToApply=Velocity; Effect->bAdditiveVelocity=false; Effect->ForceMovementMode=DefaultModeNames::Falling; Mover->QueueInstantMovementEffect(Effect);
}
void AProject_JMoverPawn::ProduceInput_Implementation(int32,FMoverInputCmdContext& InputCmd)
{
 FVector Direction=ConsumeMovementInputVector();
 float Speed=bSprint&&!bStrafe?750.f:500.f;
 if(NavMover&&NavMover->ConsumeNavMovementData(NavIntent,NavVelocity))
 {
  const auto* Settings=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
  const float MaxSpeed=Settings?Settings->MaxSpeed:Speed;
  if(!NavVelocity.IsNearlyZero()) { Speed=FMath::Min(NavVelocity.Size(),MaxSpeed); Direction=NavVelocity.GetSafeNormal(); }
  else { Speed=MaxSpeed; Direction=NavIntent.GetClampedToMaxSize(1.f); }
 }
 if(!MoveIntent.IsNearlyZero()) Direction=FRotator(0,GetControlRotation().Yaw,0).RotateVector(FVector(MoveIntent.Y,MoveIntent.X,0));
 PopulateMoverInput(InputCmd,Direction.GetClampedToMaxSize(1.f),bStrafe,bSprint,Speed);
}
void AProject_JMoverPawn::PopulateMoverInput(FMoverInputCmdContext& InputCmd,FVector WorldIntent,bool bUseStrafe,bool bUseSprint,float Speed,bool bAllowJump,bool bUseVelocityInput,float AnalogAmount)
{
 auto& Input=InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
 auto& Intent=InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>(); Input=FCharacterDefaultInputs(); Intent=FProject_JMoverInputs();
 // Unpossessed/attached authority pawns still participate in simulation. An
 // Invalid input type is not a neutral move, and old edges cannot cross owners.
 Input.SetMoveInput(EMoveInputType::DirectionalIntent,FVector::ZeroVector);
 if(!Controller) { bJumpPressed=bJumpJustPressed=false; return; }
 Input.ControlRotation=Controller->GetControlRotation(); Intent.bStrafe=bUseStrafe; Intent.bSprint=bUseSprint;Intent.JumpRequestId=JumpRequestId;
 // Velocity requests are appropriate for path following, which must stop at its
 // destination. Player intent must retain acceleration/braking and air momentum.
 const EMoveInputType InputType=bUseVelocityInput?EMoveInputType::Velocity:EMoveInputType::DirectionalIntent;
 Input.SetMoveInput(InputType,bUseVelocityInput?WorldIntent*Speed:WorldIntent);
 if(bUseStrafe && (!WorldIntent.IsNearlyZero() || Mover->IsAirborne())) LastOrientation=FRotator(0,Input.ControlRotation.Yaw,0).Vector(); else if(!bUseStrafe && !WorldIntent.IsNearlyZero()) LastOrientation=WorldIntent.GetSafeNormal();
 if(Mover->HasFeaturesWithTag(Mover_AnimRootMotion_Montage,false)) LastOrientation=GetActorForwardVector();
 Input.OrientationIntent=LastOrientation; Input.bIsJumpPressed=bJumpPressed&&bAllowJump; Input.bIsJumpJustPressed=bJumpJustPressed&&Input.bIsJumpPressed;
 if(LastControlFrame!=GFrameCounter)
 {
  const float Analog=FMath::Clamp(AnalogAmount>=0.f?AnalogAmount:WorldIntent.Size(),0.f,1.f);
  SmoothedAnalogInputAmount=FMath::FInterpConstantTo(SmoothedAnalogInputAmount,Analog,GetWorld()->GetDeltaSeconds(),Analog>SmoothedAnalogInputAmount?100.f:2.f);
  ControlYawRate=LastControlFrame==MAX_uint64?0.f:FMath::FindDeltaAngleDegrees(LastControlYaw,Input.ControlRotation.Yaw)/FMath::Max(GetWorld()->GetDeltaSeconds(),UE_SMALL_NUMBER);
  LastControlFrame=GFrameCounter;LastControlYaw=Input.ControlRotation.Yaw;
 }
 // Cache registrations, not values: input generation is a per-command hot path.
 // Retry an absent data-driven registration and preserve live editor tuning.
 static IConsoleVariable* AnalogStyle=nullptr;
 if(!AnalogStyle) AnalogStyle=IConsoleManager::Get().FindConsoleVariable(TEXT("DDCvar.AnalogInputStyle"));
 Intent.bWalk=bWalk || (!bUseVelocityInput && (!AnalogStyle || AnalogStyle->GetInt()==1) && SmoothedAnalogInputAmount<=.7f);
 Intent.ControlRotationRate=ControlYawRate;
 const FVector Direction=Mover->IsAirborne()?GetVelocity().GetSafeNormal2D():WorldIntent;
 if(bUseStrafe && !Direction.IsNearlyZero() && RotationOffsetChooser)
 {
  float Angle=FMath::FindDeltaAngleDegrees(LastOrientation.Rotation().Yaw,Direction.Rotation().Yaw);
  // Source ClampAngleToPreventConstantFlippingAt180, only while previously forward.
  if(LastMovementDirection==0)
  {
   const float PreviousOffset=LastRotationOffset==0.f?FMath::FindDeltaAngleDegrees(LastOrientation.Rotation().Yaw,GetActorRotation().Yaw):LastRotationOffset;
   if(PreviousOffset>0.f && Angle>=-180.f && Angle<=-170.f) Angle=179.f;
   else if(PreviousOffset<=0.f && Angle>=170.f && Angle<=180.f) Angle=-179.f;
  }
  Intent.MovementDirection=uint8(bUseSprint?EProject_JPlayerMovementDirection::Forward:
   GetStrafeDirectionPolicy().Resolve(Angle,static_cast<EProject_JPlayerMovementDirection>(LastMovementDirection)));
  FProject_JPlayerRotationOffsetQuery Query;
  Query.MovementMode=Mover->IsAirborne()?EProject_JPlayerMovementMode::InAir:EProject_JPlayerMovementMode::OnGround;
  Query.MovementDirection=static_cast<EProject_JPlayerMovementDirection>(Intent.MovementDirection);
  FChooserEvaluationContext Context;Context.AddStructParam(Query);
  UChooserTable::EvaluateChooser(Context,RotationOffsetChooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
  {
   if(const auto* Curve=Cast<UCurveFloat>(Object)) {Intent.RotationOffset=Curve->GetFloatValue(Angle);return FObjectChooserBase::EIteratorStatus::Stop;}
   return FObjectChooserBase::EIteratorStatus::Continue;
  }));
 }
 LastRotationOffset=Intent.RotationOffset;LastMovementDirection=Intent.MovementDirection;
 if(auto* Base=Mover->GetMovementBase())
 {
  const FName Bone=Mover->GetMovementBaseBoneName(); FVector RelativeMove,RelativeOrientation;
  UBasedMovementUtils::TransformWorldDirectionToBased(Base,Bone,Input.GetMoveInput(),RelativeMove); UBasedMovementUtils::TransformWorldDirectionToBased(Base,Bone,Input.OrientationIntent,RelativeOrientation);
  Input.SetMoveInput(InputType,RelativeMove); Input.OrientationIntent=RelativeOrientation; Input.bUsingMovementBase=true; Input.MovementBase=Base; Input.MovementBaseBoneName=Bone;
 }
 bJumpJustPressed=false;
}
