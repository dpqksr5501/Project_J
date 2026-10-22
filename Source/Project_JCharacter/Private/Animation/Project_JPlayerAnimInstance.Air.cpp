#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JAnimationPresentationHelpers.h"
#include "Animation/AnimSequenceBase.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Animation/AnimClassInterface.h"
#include "Chooser.h"
#include "ChooserTypes.h"
#include "Project_JPlayerCharacter.h"
#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/World.h"

float ProjectJ::Animation::Air::ResolveLiveAngle(const FVector& Velocity, bool bHasInput, float InputAngle,
 float ActorYaw, float DesiredFacingYaw, EProject_JLocomotionRotationMode RotationMode)
{
 // Represent actual travel against the live facing. OTM now turns in air;
 // choosing from input alone would keep a backward clip after the body turns.
 // Strafe uses requested camera facing before the capsule finishes its turn.
 if (Velocity.SizeSquared2D()>100.f)
  return FMath::FindDeltaAngleDegrees(RotationMode==EProject_JLocomotionRotationMode::Strafe?DesiredFacingYaw:ActorYaw,Velocity.Rotation().Yaw);
 return bHasInput?InputAngle:0.f;
}

FProject_JPlayerAirQuery ProjectJ::Animation::Air::ResolveEntryQuery(const FProject_JPlayerAirQuery& Requested)
{
 auto Query=Requested;
 const bool bForward=Query.Direction==EProject_JStateControllerStrafeDirection::Forward ||
  Query.Direction==EProject_JStateControllerStrafeDirection::ForwardLeft || Query.Direction==EProject_JStateControllerStrafeDirection::ForwardRight;
 // The authored sprint jump family has forward clips only. A lateral/backward
 // request uses the matching Run jump, including when sprint was released in air.
 if (Query.Gait==EProject_JLocomotionGaitIntent::Sprint && !bForward) Query.Gait=EProject_JLocomotionGaitIntent::Run;
 return Query;
}

FProject_JPlayerAirQuery ProjectJ::Animation::Air::ResolveReselectionQuery(
 const FProject_JPlayerAirQuery& Live,const FProject_JPlayerAirQuery& Committed,float LiveAngle)
{
 auto Query=Live;
 Query.Foot=Committed.Foot;Query.State=Committed.State;
 Query.Direction=Presentation::ResolveStateControllerStrafeDirection(LiveAngle,Committed.Direction,10.f);
 return ResolveEntryQuery(Query);
}

void UProject_JPlayerAnimInstance::BuildProjectAirFrame(FProject_JAnimThreadSafeData& Data)
{
 auto& Frame=Data.PlayerLocomotionAir;
 Frame.Chooser=AirAnimationChooser;
 Frame.Seconds=GetWorld()?GetWorld()->GetTimeSeconds():0.;
 const auto* Pawn=Cast<AProject_JMoverPawn>(OwningPawn);
 const auto* Life=Pawn?Pawn->GetMover()->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>():nullptr;
 Frame.Episode=Life?Life->AirSequence:0;
 Frame.Query.bFallOff=Life?!Life->bJumpEpisode:!Data.Air.bIsJumping;
 Frame.Query.RotationMode=Data.LocomotionContext.RotationMode;
 Frame.Query.Gait=Data.Ground.bWantsSprint ? EProject_JLocomotionGaitIntent::Sprint : Data.LocomotionContext.GaitIntent;
 EProject_JStateControllerFootSelectionReason Reason;
 Frame.Query.Foot=ResolveStateControllerFootFromContactCurves(true,Reason);
 const FVector Planar(Data.Movement.Velocity.X,Data.Movement.Velocity.Y,0.);
 Frame.bHasLiveDirection=Planar.SizeSquared()>100.f || Data.Input.bHasMoveInput;
 Frame.LiveDirectionDegrees=ProjectJ::Animation::Air::ResolveLiveAngle(Planar,Data.Input.bHasMoveInput,Data.Input.MovementDirection,
  OwningPawn?OwningPawn->GetActorRotation().Yaw:0.f,Data.LocomotionContext.DesiredFacingYaw,Frame.Query.RotationMode);
 const auto* Profile=GetLocomotionProfile();
 const auto Policy=Profile?Profile->AirPolicy:FProject_JPlayerAirPolicy();
 Frame.bEnableReselection=Policy.bEnableAirJumpReselection;
 Frame.MaxProgressRatio=Policy.AirJumpMaxProgressRatio;
 Frame.MinBlendProgressRatio=Policy.AirJumpMinBlendProgressRatio;
 Frame.BlendTime=Policy.AirJumpBlendTime;
 Frame.Cooldown=Policy.AirJumpReselectCooldown;
 if (Frame.Query.RotationMode==EProject_JLocomotionRotationMode::Strafe)
  Frame.Query.Direction=ProjectJ::Animation::Presentation::ResolveStateControllerStrafeDirection(Frame.LiveDirectionDegrees,EProject_JStateControllerStrafeDirection::Forward,0.f);
 else
 {
  const float Angle=Frame.LiveDirectionDegrees;
  const float Threshold=Policy.OTMJumpLaunchAngleThreshold>0.f?Policy.OTMJumpLaunchAngleThreshold:45.f;
  Frame.Query.Direction=FMath::Abs(Angle)<=Threshold?EProject_JStateControllerStrafeDirection::Forward:
   Angle<0.f && Angle>=-135.f?EProject_JStateControllerStrafeDirection::Left:
   Angle>0.f && Angle<=135.f?EProject_JStateControllerStrafeDirection::Right:EProject_JStateControllerStrafeDirection::Backward;
 }
}

void UProject_JPlayerAnimInstance::BeginProjectAir(bool bLoop)
{
 const auto& Frame=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
 const bool bSameEpisode=Frame.Air.bIsInAir && ActiveAirEpisode==Frame.PlayerLocomotionAir.Episode && AirSelectedAsset;
 const bool bKeepPlayback=bSameEpisode && (!bLoop || bAirLoop);
 // Native GASP may re-enter the air state when direction changes. The project's
 // jump player belongs to the physical episode; re-entry cannot rewind it.
 if (bKeepPlayback) bLoop=bAirLoop;
 else { bAirLoop=bLoop; bAirEntryPending=true; }
 PlayerNoValidAnim = false;
 PlayerBlendStackInputs.Loop=bLoop;
 if (bLoop) ++AirLoopEntryCount; else ++AirTransitionEntryCount;
 PlayerStateName=bLoop?FName(TEXT("In Air Loop")):FName(TEXT("In Air Transition"));
 PlayerStateMachineState=bLoop?EProject_JPlayerPlaybackState::AirLoop:EProject_JPlayerPlaybackState::AirTransition;
}

bool UProject_JPlayerAnimInstance::IsProjectAirTransitionComplete() const
{
 const auto& Proxy=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>();
 // A state machine can evaluate several transitions before its state-entry callback
 // runs. Physical air must block the shared rule's stale ground-clock result too.
 const bool bAirTransition=Proxy.GetThreadSafeData().Air.bIsInAir && !Proxy.GetThreadSafeData().PlayerLocomotionAir.Query.bFallOff;
 if (bAirTransition && (bAirEntryPending || !AirSelectedAsset || ActiveAirEpisode!=Proxy.GetThreadSafeData().PlayerLocomotionAir.Episode)) return false;
 const auto* Interface=Proxy.GetAnimClassInterface();
 if (!Interface) return false;
 for (int32 Index=0;Index<Interface->GetAnimNodeProperties().Num();++Index)
 {
  const auto* Stack=Proxy.GetNodeFromIndex<FAnimNode_BlendStack_Standalone>(Index);
  if (Stack && Stack->GetCachedBlendWeight()>ZERO_ANIMWEIGHT_THRESH && Stack->GetAnimAsset() && (!bAirTransition || Stack->GetAnimAsset()==AirSelectedAsset))
   return !Stack->AnimPlayers.IsEmpty() && !Stack->AnimPlayers[0].IsLooping() && Stack->GetCurrentAssetLength()-Stack->GetCurrentAssetTimePlayRateAdjusted()<=(bAirTransition?LastAirBlendTime:.75f);
 }
 return false;
}

bool UProject_JPlayerAnimInstance::UsesProjectJumpAir() const
{
 const auto& Data=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
 return Data.Air.bIsInAir && Data.Air.bIsJumping;
}

void UProject_JPlayerAnimInstance::UpdateProjectAir(const FAnimUpdateContext&,const FAnimNodeReference& Node)
{
 const auto& Data=GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
 const auto& Frame=Data.PlayerLocomotionAir;
 // Falling off an edge keeps the original GASP Chooser entry/player. Only an
 // actual Project J jump owns directional reselection and episode playhead.
 if(Frame.Query.bFallOff) {bAirEntryPending=false;AirSelectedAsset=nullptr;return;}
 const auto CurrentState=PlayerStateMachineState;
 constexpr auto TransitionState=EProject_JPlayerPlaybackState::AirTransition;
 constexpr auto LoopState=EProject_JPlayerPlaybackState::AirLoop;
 if (CurrentState!=TransitionState && CurrentState!=LoopState) { bAirEntryPending=false; AirSelectedAsset=nullptr; return; }
 EAnimNodeReferenceConversionResult Conversion;
 const auto Blend=UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(Node,Conversion);
 if (Conversion!=EAnimNodeReferenceConversionResult::Succeeded) return;
 const bool bEntry=bAirEntryPending || ActiveAirEpisode!=Frame.Episode;
 auto Query=bEntry?ProjectJ::Animation::Air::ResolveEntryQuery(Frame.Query):CommittedAirQuery;
 Query.State=CurrentState==LoopState?EProject_JStateControllerPresentationState::InAirLoop:EProject_JStateControllerPresentationState::TransitionToInAir;
 const float Playhead=UBlendStackAnimNodeLibrary::GetCurrentAssetTime(Blend);
 bool bReselect=false;
 if (!bEntry && Query.State==EProject_JStateControllerPresentationState::TransitionToInAir && Frame.bEnableReselection && !Query.bFallOff && Data.Air.bIsInAir && Frame.bHasLiveDirection)
 {
  const auto* Sequence=Cast<UAnimSequenceBase>(AirSelectedAsset);
  const float Length=Sequence?Sequence->GetPlayLength():0.f;
  const float Wait=FMath::Max(FMath::Max(0.f,Frame.Cooldown),LastAirBlendTime*FMath::Clamp(Frame.MinBlendProgressRatio,.3f,1.f));
  const bool bReady=LastAirReselection<0.?Playhead-AirEntryStartTime>=.05f:Frame.Seconds-LastAirReselection>=Wait;
  const auto Live=ProjectJ::Animation::Air::ResolveReselectionQuery(Frame.Query,Query,Frame.LiveDirectionDegrees);
  const bool bChanged=Live.Direction!=Query.Direction || Live.Gait!=Query.Gait || Live.RotationMode!=Query.RotationMode;
  if (bReady && Length>.05f && Playhead<Length*FMath::Clamp(Frame.MaxProgressRatio,.1f,1.f) && bChanged) { Query=Live; bReselect=true; }
 }
 if (!bEntry && !bReselect) return;
 FChooserEvaluationContext ChooserContext;
 ChooserContext.AddStructParam(Query);
 FChooserPlayerSettings PlayerSettings; ChooserContext.AddStructParam(PlayerSettings);
 FProject_JStateControllerChooserOutput Output; ChooserContext.AddStructParam(Output);
 UAnimationAsset* Selected=nullptr;
 if (Frame.Chooser) UChooserTable::EvaluateChooser(ChooserContext,Frame.Chooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
 {
  Selected=Cast<UAnimationAsset>(Object);
  return Selected?FObjectChooserBase::EIteratorStatus::Stop:FObjectChooserBase::EIteratorStatus::Failed;
 }));
 if (!Selected)
 {
  // A missing new directional clip cannot cancel the currently playing jump.
  if (bEntry) PlayerNoValidAnim = true;
  return;
 }
 if (bReselect && Selected==AirSelectedAsset) { CommittedAirQuery=Query; return; }
 if (bReselect && Playhead>Selected->GetPlayLength()-.05f) return;
 const float Start=bReselect?FMath::Clamp(FMath::Max(Output.StartTime,Playhead),0.f,FMath::Max(0.f,Selected->GetPlayLength()-.05f)):Output.StartTime;
 const float BlendTime=Output.BlendTime>0.f?Output.BlendTime:FMath::Max(0.f,Frame.BlendTime);
 PlayerBlendStackInputs.Anim=Selected;
 PlayerBlendStackInputs.StartTime=Start;PlayerBlendStackInputs.BlendTime=BlendTime;
 PlayerBlendStackInputs.Loop=Query.State==EProject_JStateControllerPresentationState::InAirLoop;
 PlayerBlendStackInputs.BlendProfile=Output.BlendProfile;PlayerBlendStackInputs.Tags=MoveTemp(Output.Tags);
 UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Blend);
 PlayerNoValidAnim = false;
 if (bReselect) { ++AirReselectionCount; LastAirReselectionFromTime=Playhead; LastAirReselectionToTime=Start; LastAirReselection=Frame.Seconds; }
 else { AirEntryStartTime=Start; LastAirReselection=-1.; }
 LastAirBlendTime=BlendTime;
 CommittedAirQuery=Query; AirSelectedAsset=Selected; ActiveAirEpisode=Frame.Episode; bAirEntryPending=false;
}
