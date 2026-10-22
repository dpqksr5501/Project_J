#pragma once
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Project_JAnimationSnapshot.h"

/** Immutable common inputs; the generated GASP graph is the only locomotion pose provider. */
struct FProject_JCharacterAnimInstanceProxy : public FAnimInstanceProxy
{
 FProject_JCharacterAnimInstanceProxy()=default;
 explicit FProject_JCharacterAnimInstanceProxy(UAnimInstance* Instance):FAnimInstanceProxy(Instance) {}
 void QueueGameThreadData(const FProject_JAnimThreadSafeData& Data);
 const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
 /** GT-only observation of the last completed traversal; scans nodes only on demand. */
 const FProject_JAnimPlaybackSnapshot& GetLatestPostSelection() const;
protected:
 virtual void Initialize(UAnimInstance* Instance) override;
 virtual void AddReferencedObjects(UAnimInstance* Instance,FReferenceCollector& Collector) override;
 virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override;
 virtual void UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& Context,FAnimNode_Base* Root,FName Layer) override;
private:
 friend class FProjectJPlayerLocomotionSnapshotBoundaryTest;
 friend class FProjectJPlayerLocomotionMoverAimPublication;
 friend class FProjectJPlaybackObservationLifetimeTest;
 void ConsumeQueuedGameThreadData();
 void CapturePlayerLocomotionPostSelection() const;
 FProject_JAnimThreadSafeData PendingGameThreadData;
 FProject_JAnimThreadSafeData ThreadSafeData;
 mutable FProject_JAnimPlaybackSnapshot LatestPostSelection;
 uint64 PlaybackUpdateRevision=0;
 mutable uint64 CapturedPlaybackRevision=0;
 uint64 PlaybackUpdateFrame=0;
 uint64 PublishedSnapshotRevision=0;
 uint64 ConsumedSnapshotRevision=0;
 bool bUpdatingGraph=false;
};
