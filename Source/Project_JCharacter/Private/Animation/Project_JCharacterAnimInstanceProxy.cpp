#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstance.h"

void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(const FProject_JAnimThreadSafeData& Data)
{
 check(IsInGameThread());
 PendingGameThreadData=Data;
 ++PublishedSnapshotRevision;
}
void FProject_JCharacterAnimInstanceProxy::Initialize(UAnimInstance* Instance)
{
 FAnimInstanceProxy::Initialize(Instance);
 PendingGameThreadData={}; ThreadSafeData={}; LatestPostSelection={};
 PublishedSnapshotRevision=ConsumedSnapshotRevision=0;
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
 ThreadSafeData=PendingGameThreadData;
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
 // Linked layers retain the outer traversal's snapshot. NativeUpdate may have
 // published after PreUpdate, so consume its final publication at this boundary.
 if(bUpdatingGraph) { FAnimInstanceProxy::UpdateAnimationNode_WithRoot(Context,Root,Layer); return; }
 TGuardValue<bool> Scope(bUpdatingGraph,true);
 ConsumeQueuedGameThreadData();
 ThreadSafeData.DeltaTime=Context.GetDeltaTime();
 FAnimInstanceProxy::UpdateAnimationNode_WithRoot(Context,Root,Layer);
 CapturePlayerLocomotionPostSelection();
}
