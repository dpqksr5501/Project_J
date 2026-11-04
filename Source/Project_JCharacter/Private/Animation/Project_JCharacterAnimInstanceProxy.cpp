#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(const FProject_JAnimThreadSafeData& Data)
{
 check(IsInGameThread());
 PendingGameThreadData=Data;
 PendingPreparedTrajectory.Reset();
 ++PublishedSnapshotRevision;
}
void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(FProject_JAnimThreadSafeData&& Data)
{
 check(IsInGameThread());
 PendingGameThreadData=MoveTemp(Data);
 PendingPreparedTrajectory.Reset();
 ++PublishedSnapshotRevision;
}
void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(FProject_JAnimThreadSafeData&& Data,FProject_JPreparedAnimTrajectory&& Prediction)
{
 check(IsInGameThread());
 PendingGameThreadData=MoveTemp(Data);
 PendingPreparedTrajectory.Emplace(MoveTemp(Prediction));
 ++PublishedSnapshotRevision;
}
void FProject_JCharacterAnimInstanceProxy::RetirePreparedTrajectory()
{
 check(IsInGameThread());
 PendingPreparedTrajectory.Reset();PreparedTrajectory.Reset();
 CompletedPreparedRevision=CompletedPreparedSourceGeneration=0;
}
bool FProject_JCharacterAnimInstanceProxy::CompletePreparedTrajectory()
{
 if(!PreparedTrajectory.IsSet()) return true;
 const bool bFlying=PreparedTrajectory->Kind==FProject_JPreparedAnimTrajectory::EKind::NativeFlying;
 TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(bFlying?TEXT("ProjectJ_Anim_PreparedFlyingPrediction"):TEXT("ProjectJ_Anim_PreparedGroundPrediction"));
 TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(bFlying?
  (IsInGameThread()?TEXT("ProjectJ_Anim_PreparedFlyingPrediction_GameThread"):TEXT("ProjectJ_Anim_PreparedFlyingPrediction_Worker")):
  (IsInGameThread()?TEXT("ProjectJ_Anim_PreparedGroundPrediction_GameThread"):TEXT("ProjectJ_Anim_PreparedGroundPrediction_Worker")));
 const uint64 Source=PreparedTrajectory->SourceGeneration;
 const bool bCompleted=PreparedTrajectory->Compute(ThreadSafeData,PreparedTrajectoryScratch);
 PreparedTrajectory.Reset();
 if(bCompleted)
 {
  CompletedPreparedRevision=ConsumedSnapshotRevision;
  CompletedPreparedSourceGeneration=Source;
 }
 return bCompleted;
}
void FProject_JCharacterAnimInstanceProxy::PostUpdate(UAnimInstance* Instance) const
{
 FAnimInstanceProxy::PostUpdate(Instance);
 check(IsInGameThread());
 // Proxy output remains intact for completed-frame readers and linked graphs.
 if(CompletedPreparedRevision && CompletedPreparedRevision==ConsumedSnapshotRevision &&
  ConsumedSnapshotRevision==PublishedSnapshotRevision && ThreadSafeData.Movement.bHasTrajectory)
  if(auto* Player=Cast<UProject_JPlayerAnimInstance>(Instance))
   Player->CommitPreparedTrajectorySnapshot(ThreadSafeData,CompletedPreparedSourceGeneration);
}
void FProject_JCharacterAnimInstanceProxy::Initialize(UAnimInstance* Instance)
{
 FAnimInstanceProxy::Initialize(Instance);
 PendingGameThreadData={}; ThreadSafeData={}; LatestPostSelection={};
 RetirePreparedTrajectory();
 PublishedSnapshotRevision=ConsumedSnapshotRevision=0;
 PlaybackUpdateRevision=CapturedPlaybackRevision=PlaybackUpdateFrame=0;
 bUpdatingGraph=false;
}
void FProject_JCharacterAnimInstanceProxy::AddReferencedObjects(UAnimInstance* Instance,FReferenceCollector& Collector)
{
 FAnimInstanceProxy::AddReferencedObjects(Instance,Collector);
 Collector.AddPropertyReferencesWithStructARO(FProject_JAnimThreadSafeData::StaticStruct(),&PendingGameThreadData,Instance);
 Collector.AddPropertyReferencesWithStructARO(FProject_JAnimThreadSafeData::StaticStruct(),&ThreadSafeData,Instance);
}
void FProject_JCharacterAnimInstanceProxy::ConsumeQueuedGameThreadData()
{
 if(ConsumedSnapshotRevision==PublishedSnapshotRevision) return;
 ThreadSafeData=MoveTemp(PendingGameThreadData);
 PreparedTrajectory=MoveTemp(PendingPreparedTrajectory);
 PendingPreparedTrajectory.Reset();
 CompletedPreparedRevision=CompletedPreparedSourceGeneration=0;
 // Moving arrays does not clear scalar UObject references in the source.
 // Retire them too: the pending slot must not pin an already consumed asset.
 PendingGameThreadData={};
 ConsumedSnapshotRevision=PublishedSnapshotRevision;
}
void FProject_JCharacterAnimInstanceProxy::PreUpdate(UAnimInstance* Instance,float DeltaSeconds)
{
 FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
 ConsumeQueuedGameThreadData();
 ThreadSafeData.DeltaTime=DeltaSeconds;
}
void FProject_JCharacterAnimInstanceProxy::UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& Context,FAnimNode_Base* Root,FName Layer)
{
 // NativeThreadSafeUpdate already consumed the final GT publication before
 // state decisions. Linked layers retain that same snapshot during traversal.
 if(bUpdatingGraph) { FAnimInstanceProxy::UpdateAnimationNode_WithRoot(Context,Root,Layer); return; }
 TGuardValue<bool> Scope(bUpdatingGraph,true);
 ThreadSafeData.DeltaTime=Context.GetDeltaTime();
 FAnimInstanceProxy::UpdateAnimationNode_WithRoot(Context,Root,Layer);
 // Observation metadata is cheap and belongs to this completed traversal.
 // The diagnostic node walk is deferred until a GT consumer requests it.
 PlaybackUpdateFrame=GFrameCounter;
 ++PlaybackUpdateRevision;
}
