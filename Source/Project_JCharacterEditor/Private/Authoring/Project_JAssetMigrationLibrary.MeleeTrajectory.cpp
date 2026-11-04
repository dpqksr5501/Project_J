#include "Authoring/Project_JAssetMigrationLibrary.h"
#include "Authoring/Project_JCanonicalMeleeAuthoring.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/IAnimationSequenceCompiler.h"
#include "SkinnedAssetCompiler.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/Skeleton.h"
#include "Animation/SkeletonRemapping.h"
#include "Animation/SkeletonRemappingRegistry.h"
#include "Animation/AnimCompressionTypes.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Misc/MemStack.h"
#include "AnimPose.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Hash/Blake3.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr double SamplingHz=120.0;
const TCHAR* SamplingPolicy=TEXT("ProjectJ.CanonicalMelee.Raw.v1;120HzAbsoluteMontageGrid+ExactEndpoints;Retarget=1;ExtractRootMotion=1;IncorporateRootMotionIntoPose=0;Curves=1;MeshComponentSpace;NoIKNoPhysicsNoCompression");

bool IsFiniteVector(const FVector& Value)
{
    return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}

bool SamePath(const FProject_JMeleeTrajectory& A,const FProject_JMeleeTrajectory& B)
{
    return FProject_JMeleeTrajectory::StaticStruct()->CompareScriptStruct(&A,&B,PPF_None);
}

// A local immutable read description; all callers pin its source objects before evaluation/edit callbacks.
struct FCanonicalSource
{
    UAnimMontage* Montage=nullptr;
    UAnimSequence* Sequence=nullptr;
    USkeleton* Skeleton=nullptr;
    USkeleton* TargetSkeleton=nullptr;
    FAnimSegment Segment;
    int32 NotifyIndex=INDEX_NONE;
    FName SocketName;
    FName TraceBone;
    FTransform SocketLocal=FTransform::Identity;
    bool bSocket=false;
    float Start=0,End=0;
    TArray<float> Times;
    FString MetadataStamp;
};

bool MakeTimes(float Start,float End,TArray<float>& Out,FString& Failure)
{
    if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || Start<0 || End<=Start || End>MAX_int32/SamplingHz)
    { Failure=TEXT("Melee notify window is not a finite forward interval."); return false; }
    const int64 First=FMath::FloorToInt64(double(Start)*SamplingHz)+1;
    const int64 Last=FMath::CeilToInt64(double(End)*SamplingHz)-1;
    if (Last-First+3>FProject_JMeleeTrajectory::MaxKeys)
    { Failure=TEXT("120 Hz absolute montage grid exceeds the 64-key contract; no decimation was applied."); return false; }
    Out.Reset(); Out.Add(Start);
    for (int64 Grid=First;Grid<=Last;++Grid)
    {
        const float Time=float(double(Grid)/SamplingHz);
        if (Time>Start && Time<End && Time>Out.Last()) Out.Add(Time);
    }
    Out.Add(End);
    if (Out.Num()>FProject_JMeleeTrajectory::MaxKeys)
    { Failure=TEXT("Sampling grid exceeds the key cap."); return false; }
    return true;
}

bool AppendReference(FBufferArchive& Bytes,const FReferenceSkeleton& Ref,FString& Failure)
{
    int32 Count=Ref.GetNum(); Bytes<<Count;
    for (int32 Bone=0;Bone<Count;++Bone)
    {
        FString Name=Ref.GetBoneName(Bone).ToString(); Bytes<<Name;
        int32 Parent=Ref.GetParentIndex(Bone); Bytes<<Parent;
        FTransform Pose=Ref.GetRefBonePose()[Bone];
        if (Pose.ContainsNaN()) { Failure=TEXT("Reference pose contains a non-finite transform."); return false; }
        Bytes<<Pose;
    }
    return true;
}

void AppendProperties(FBufferArchive& Bytes,const UObject& Object)
{
    // Public reflected authoring data, with normal UObject export semantics. No private-memory access.
    FString ClassPath=Object.GetClass()->GetPathName(); Bytes<<ClassPath;
    for (TFieldIterator<FProperty> Field(Object.GetClass());Field;++Field)
    {
        if (Field->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated)) continue;
        FString Name=Field->GetName(),Text;
        Field->ExportTextItem_Direct(Text,Field->ContainerPtrToValuePtr<void>(&Object),nullptr,const_cast<UObject*>(&Object),PPF_None);
        Bytes<<Name<<Text;
    }
}

FString OtherAttackFields(const UProject_JAttackDefinition& Attack)
{
    FBufferArchive Bytes;
    for (TFieldIterator<FProperty> Field(Attack.GetClass());Field;++Field)
    {
        if (Field->GetFName()==GET_MEMBER_NAME_CHECKED(UProject_JAttackDefinition,MeleeTrajectory) ||
            Field->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated)) continue;
        FString Name=Field->GetName(),Text;
        Field->ExportTextItem_Direct(Text,Field->ContainerPtrToValuePtr<void>(&Attack),nullptr,const_cast<UProject_JAttackDefinition*>(&Attack),PPF_None);
        Bytes<<Name<<Text;
    }
    return LexToString(FBlake3::HashBuffer(Bytes.GetData(),Bytes.Num()));
}

bool CaptureSource(const UProject_JAttackDefinition& Attack,USkeletalMesh& Mesh,FCanonicalSource& Out,FString& Failure)
{
    const auto Fail=[&](const TCHAR* Why) { Failure=Why; return false; };
    if (!IsInGameThread() || !IsValid(&Attack) || !IsValid(&Mesh) || (GEditor && GEditor->PlayWorld))
        return Fail(TEXT("Canonical melee authoring requires live selected assets on the editor game thread outside PIE."));
    auto* Montage=Attack.Montage.Get();
    if (!IsValid(Montage)) return Fail(TEXT("Selected montage is unavailable."));
    const TStrongObjectPtr<UProject_JAttackDefinition> KeepAttack(const_cast<UProject_JAttackDefinition*>(&Attack));
    const TStrongObjectPtr<USkeletalMesh> KeepMesh(&Mesh);
    const TStrongObjectPtr<UAnimMontage> KeepMontage(Montage);
    if (Mesh.IsCompiling())
    {
        USkinnedAsset* const SelectedMesh[]={&Mesh};
        FSkinnedAssetCompilingManager::Get().FinishCompilation(MakeArrayView(SelectedMesh));
    }
    if (!IsValid(&Attack) || !IsValid(&Mesh) || !IsValid(Montage) || Attack.Montage!=Montage || (GEditor && GEditor->PlayWorld))
        return Fail(TEXT("Selected source changed during mesh compilation completion."));
    if (!Attack.bMontageDriven || !IsValid(Montage) || !FMath::IsFinite(Attack.PlayRate) || Attack.PlayRate<=0 ||
        Attack.MovementPolicy==EProject_JAttackMovementPolicy::RootMotionWarped || !Attack.MontageSectionName.IsNone())
        return Fail(TEXT("Require forward unwarped montage execution without a selected section jump."));
    if (Mesh.IsCompiling() || Mesh.GetOutermost()->IsDirty() || Montage->GetOutermost()->IsDirty() ||
        Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1 ||
        Montage->Notifies.Num()>64 || Montage->CompositeSections.Num()!=1 ||
        Montage->CompositeSections[0].GetTime()!=0 || !Montage->CompositeSections[0].NextSectionName.IsNone())
    {
        Failure=FString::Printf(TEXT("Require clean source structure: mesh_compiling=%d mesh_dirty=%d montage_dirty=%d slots=%d segments=%d notifies=%d sections=%d section_time=%.9g next=%s"),
            Mesh.IsCompiling(),Mesh.GetOutermost()->IsDirty(),Montage->GetOutermost()->IsDirty(),Montage->SlotAnimTracks.Num(),
            Montage->SlotAnimTracks.Num()==1?Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num():-1,Montage->Notifies.Num(),Montage->CompositeSections.Num(),
            Montage->CompositeSections.Num()?Montage->CompositeSections[0].GetTime():-1.f,
            Montage->CompositeSections.Num()?*Montage->CompositeSections[0].NextSectionName.ToString():TEXT("<none>"));
        return false;
    }
    const FAnimSegment Segment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
    auto* Sequence=Cast<UAnimSequence>(Segment.GetAnimReference().Get());
    const TStrongObjectPtr<UAnimSequence> KeepSequence(Sequence);
    if (IsValid(Sequence))
    {
        // Selected offline authoring only: finish existing tasks through the
        // supported API before observing source inputs. Never a runtime wait.
        UAnimSequence* const Selected[]={Sequence};
        UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(MakeArrayView(Selected));
    }
    // Compilation postprocessing can call editor listeners. Do not retain an
    // array reference or accept another selected source after that boundary.
    if (!IsValid(&Attack) || !IsValid(&Mesh) || !IsValid(Montage) || Attack.Montage!=Montage || (GEditor && GEditor->PlayWorld) ||
        !Attack.bMontageDriven || !FMath::IsFinite(Attack.PlayRate) || Attack.PlayRate<=0 || !Attack.MontageSectionName.IsNone() ||
        Attack.MovementPolicy==EProject_JAttackMovementPolicy::RootMotionWarped || Mesh.IsCompiling() || Mesh.GetOutermost()->IsDirty() ||
        Montage->GetOutermost()->IsDirty() || Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1 ||
        Montage->Notifies.Num()>64 || Montage->CompositeSections.Num()!=1 || Montage->CompositeSections[0].GetTime()!=0 ||
        !Montage->CompositeSections[0].NextSectionName.IsNone())
        return Fail(TEXT("Selected source changed during compilation completion."));
    const FAnimSegment& CurrentSegment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
    if (CurrentSegment.GetAnimReference()!=Segment.GetAnimReference() || CurrentSegment.StartPos!=Segment.StartPos ||
        CurrentSegment.AnimStartTime!=Segment.AnimStartTime || CurrentSegment.AnimEndTime!=Segment.AnimEndTime ||
        CurrentSegment.AnimPlayRate!=Segment.AnimPlayRate || CurrentSegment.LoopingCount!=Segment.LoopingCount)
        return Fail(TEXT("Selected segment changed during compilation completion."));
    if (!IsValid(Sequence) || !Sequence->GetDataModel() || Sequence->IsValidAdditive() ||
        Sequence->GetOutermost()->IsDirty() || !FMath::IsFinite(Sequence->RateScale) || Sequence->RateScale<=0 ||
        !FMath::IsFinite(Segment.AnimPlayRate) || Segment.AnimPlayRate<=0 || Segment.LoopingCount!=1 ||
        FMath::IsNearlyZero(Segment.AnimPlayRate*Sequence->RateScale) || !FMath::IsFinite(Segment.GetEndPos()) ||
        !FMath::IsFinite(Segment.StartPos) || !FMath::IsFinite(Segment.AnimStartTime) || !FMath::IsFinite(Segment.AnimEndTime) ||
        Segment.StartPos<0 || Segment.AnimStartTime<0 || Segment.AnimEndTime<=Segment.AnimStartTime || Segment.AnimEndTime>Sequence->GetPlayLength())
        return Fail(TEXT("Require one clean non-additive RAW sequence with a finite forward non-looping segment."));
    USkeleton* Skeleton=Sequence->GetSkeleton();
    USkeleton* TargetSkeleton=Mesh.GetSkeleton();
    if (!IsValid(Skeleton) || !IsValid(TargetSkeleton) || Skeleton!=Montage->GetSkeleton() ||
        Skeleton->GetOutermost()->IsDirty() || TargetSkeleton->GetOutermost()->IsDirty())
        return Fail(TEXT("Sequence/montage require one clean source skeleton and the selected mesh requires a clean target skeleton."));
    const TStrongObjectPtr<USkeleton> KeepSourceSkeleton(Skeleton),KeepTargetSkeleton(TargetSkeleton);
    if (TargetSkeleton!=Skeleton)
    {
        const auto Declares=[](const USkeleton& Owner,const USkeleton& Other)
        {
            return Owner.GetCompatibleSkeletons().ContainsByPredicate([&](const TSoftObjectPtr<USkeleton>& Item)
                {return Item.ToSoftObjectPath()==FSoftObjectPath(&Other);});
        };
        // Do not let the editor's global "all compatible" override admit an unrelated rig.
        if ((!Declares(*TargetSkeleton,*Skeleton) && !Declares(*Skeleton,*TargetSkeleton)) || !TargetSkeleton->IsCompatibleForEditor(Skeleton))
            return Fail(TEXT("Selected mesh must use a declared native compatible skeleton."));
        // A distinct target is sampled through explicit native name remapping,
        // never through the RAW model's source-index-to-target-index shortcut.
        const FReferenceSkeleton& SourceRef=Skeleton->GetReferenceSkeleton();
        const FReferenceSkeleton& TargetRef=TargetSkeleton->GetReferenceSkeleton();
        if (!SourceRef.GetNum() || !TargetRef.GetNum() || SourceRef.GetBoneName(0)!=TargetRef.GetBoneName(0))
            return Fail(TEXT("Compatible RAW evaluation requires named source/target roots."));
        TArray<FName> TrackedBones;Sequence->GetDataModel()->GetBoneTrackNames(TrackedBones);
        for (const FName Bone:TrackedBones)
            if (SourceRef.FindBoneIndex(Bone)==INDEX_NONE)
                return Fail(TEXT("RAW track has no source-skeleton bone."));
    }
    if (!IsValid(Skeleton) || !IsValid(TargetSkeleton) || Sequence->GetSkeleton()!=Skeleton || Montage->GetSkeleton()!=Skeleton ||
        Mesh.GetSkeleton()!=TargetSkeleton || Skeleton->GetOutermost()->IsDirty() || TargetSkeleton->GetOutermost()->IsDirty() ||
        !TargetSkeleton->IsCompatibleMesh(&Mesh))
        return Fail(TEXT("Selected source/target skeleton binding changed or does not match the selected mesh."));
    if (Attack.MovementPolicy==EProject_JAttackMovementPolicy::RootMotionMontage && !Sequence->bEnableRootMotion)
        return Fail(TEXT("Root-motion movement requires an actual root-motion source sequence."));
    int32 MeleeIndex=INDEX_NONE;
    for (int32 Index=0;Index<Montage->Notifies.Num();++Index)
    {
        const auto* Melee=Cast<UProject_JAnimNotifyState_MeleeHit>(Montage->Notifies[Index].NotifyStateClass.Get());
        if (!Melee) continue;
        if (MeleeIndex!=INDEX_NONE || Melee->GetClass()!=UProject_JAnimNotifyState_MeleeHit::StaticClass())
            return Fail(TEXT("Require exactly one native melee hit window; custom notify subclasses require separate qualification."));
        MeleeIndex=Index;
    }
    if (MeleeIndex==INDEX_NONE) return Fail(TEXT("No native melee window exists in the selected montage."));
    const FAnimNotifyEvent& Event=Montage->Notifies[MeleeIndex];
    const auto* Melee=CastChecked<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get());
    const float Start=Event.GetTriggerTime(),End=Event.GetEndTriggerTime();
    if (Event.IsBranchingPoint() || Event.GetLinkedMontage()!=Montage || Start<Segment.StartPos || End>Segment.GetEndPos() || End>Montage->GetPlayLength())
        return Fail(TEXT("Require a linked queued melee window wholly within the single segment."));
    const FName Socket=Melee->GetTraceSocketName();
    if (Socket.IsNone()) return Fail(TEXT("Melee trace socket/bone is empty."));
    int32 SocketMatches=0;
    const USkeletalMeshSocket* ResolvedSocket=nullptr;
    FName TraceBone=Socket;
    FTransform SocketLocal=FTransform::Identity;
    for (const USkeletalMeshSocket* Item:Mesh.GetActiveSocketList())
    {
        if (!Item || Item->SocketName!=Socket) continue;
        if (Item==ResolvedSocket) continue; // Native active lists may alias the same socket object.
        if (ResolvedSocket) return Fail(TEXT("Selected trace name has multiple distinct active socket objects."));
        ResolvedSocket=Item;
        ++SocketMatches;
        TraceBone=Item->BoneName; SocketLocal=Item->GetSocketLocalTransform();
        if (Mesh.GetRefSkeleton().FindBoneIndex(Item->BoneName)==INDEX_NONE || Item->GetSocketLocalTransform().ContainsNaN())
            return Fail(TEXT("Selected socket has an invalid parent bone or transform."));
    }
    if (ResolvedSocket && Mesh.FindSocket(Socket)!=ResolvedSocket)
        return Fail(TEXT("Selected active socket identity disagrees with native FindSocket."));
    if (SocketMatches==0 && Mesh.GetRefSkeleton().FindBoneIndex(Socket)==INDEX_NONE)
        return Fail(TEXT("Selected trace name has no active socket or existing mesh bone."));
    if (!IsFiniteVector(Attack.HitSpec.CanonicalBladeTipOffset)) return Fail(TEXT("Static blade-tip offset is not finite."));

    FCanonicalSource Read; Read.Montage=Montage; Read.Sequence=Sequence; Read.Skeleton=Skeleton; Read.TargetSkeleton=TargetSkeleton;
    Read.Segment=Segment; Read.NotifyIndex=MeleeIndex; Read.SocketName=Socket; Read.TraceBone=TraceBone; Read.SocketLocal=SocketLocal; Read.bSocket=SocketMatches==1;
    Read.Start=Start; Read.End=End;
    if (!MakeTimes(Start,End,Read.Times,Failure)) return false;
    FBufferArchive Bytes;
    FString Policy=SamplingPolicy; Bytes<<Policy;
    FString MeshPath=Mesh.GetPathName(),MontagePath=Montage->GetPathName(),SequencePath=Sequence->GetPathName(),SkeletonPath=Skeleton->GetPathName();
    Bytes<<MeshPath<<MontagePath<<SequencePath<<SkeletonPath;
    FString Motion=UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Sequence);
    if (Motion.IsEmpty()) return Fail(TEXT("RAW source fingerprint could not be obtained."));
    Bytes<<Motion;
    if (!AppendReference(Bytes,Mesh.GetRefSkeleton(),Failure) || !AppendReference(Bytes,Skeleton->GetReferenceSkeleton(),Failure)) return false;
    int32 SkeletonBones=Skeleton->GetReferenceSkeleton().GetNum(); Bytes<<SkeletonBones;
    for (int32 Bone=0;Bone<SkeletonBones;++Bone)
    { int32 Mode=int32(Skeleton->GetBoneTranslationRetargetingMode(Bone)); Bytes<<Mode; }
    int32 VirtualCount=Skeleton->GetVirtualBones().Num(); Bytes<<VirtualCount;
    for (const auto& Virtual:Skeleton->GetVirtualBones())
    { FString A=Virtual.SourceBoneName.ToString(),B=Virtual.TargetBoneName.ToString(),C=Virtual.VirtualBoneName.ToString(); Bytes<<A<<B<<C; }
    FString RetargetName=Sequence->RetargetSource.ToString(),EffectiveRetarget=Sequence->GetRetargetTransformsSourceName().ToString();
    FString RetargetAsset=Sequence->GetRetargetSourceAsset().ToSoftObjectPath().ToString();
    Bytes<<RetargetName<<EffectiveRetarget<<RetargetAsset;
    TArray<FTransform> RetargetPose=Sequence->GetRetargetTransforms();
    for (const FTransform& Transform:RetargetPose) if (Transform.ContainsNaN()) return Fail(TEXT("Effective retarget pose is not finite."));
    Bytes<<RetargetPose;
    int32 RootLock=int32(Sequence->RootMotionRootLock.GetValue()),Interpolation=int32(Sequence->Interpolation);
    bool EnableRoot=Sequence->bEnableRootMotion,ForceRoot=Sequence->bForceRootLock,NormalizeRoot=Sequence->bUseNormalizedRootMotionScale;
    Bytes<<RootLock<<Interpolation<<EnableRoot<<ForceRoot<<NormalizeRoot;
    float SeqRate=Sequence->RateScale,MontageRate=Montage->RateScale,Length=Montage->GetPlayLength(),AttackRate=Attack.PlayRate;
    if (!FMath::IsFinite(MontageRate) || MontageRate<=0 || !FMath::IsFinite(Length) || Length<=0) return Fail(TEXT("Montage timing is invalid."));
    int32 Movement=int32(Attack.MovementPolicy); Bytes<<SeqRate<<MontageRate<<Length<<AttackRate<<Movement;
    FString Slot=Montage->SlotAnimTracks[0].SlotName.ToString(); Bytes<<Slot;
    float TrackStart=Segment.StartPos,AnimStart=Segment.AnimStartTime,AnimEnd=Segment.AnimEndTime,SegmentRate=Segment.AnimPlayRate;
    int32 Loops=Segment.LoopingCount;Bytes<<TrackStart<<AnimStart<<AnimEnd<<SegmentRate<<Loops;
    for (const auto& Section:Montage->CompositeSections)
    { FString Name=Section.SectionName.ToString(),Next=Section.NextSectionName.ToString();float Time=Section.GetTime();Bytes<<Name<<Next<<Time; }
    int32 NotifyCount=Montage->Notifies.Num(); Bytes<<NotifyCount;
    for (const auto& SourceEvent:Montage->Notifies)
    {
        FAnimNotifyEvent Copy=SourceEvent; Copy.Guid.Invalidate(); Copy.Notify=nullptr; Copy.NotifyStateClass=nullptr;
        FString Text; FAnimNotifyEvent::StaticStruct()->ExportText(Text,&Copy,nullptr,nullptr,PPF_None,nullptr); Bytes<<Text;
        for (const UObject* Payload:{static_cast<const UObject*>(SourceEvent.Notify.Get()),static_cast<const UObject*>(SourceEvent.NotifyStateClass.Get())})
        { bool Present=Payload!=nullptr;Bytes<<Present;if (Payload) AppendProperties(Bytes,*Payload); }
    }
    // The active socket list captures mesh overrides, parent names and local socket geometry.
    const auto Sockets=Mesh.GetActiveSocketList(); int32 SocketCount=Sockets.Num(); Bytes<<SocketCount;
    for (const USkeletalMeshSocket* Item:Sockets)
    { bool Present=Item!=nullptr;Bytes<<Present;if (Item) AppendProperties(Bytes,*Item); }
    bool TipEnabled=Attack.HitSpec.bUseCanonicalBladeTipOffset; FVector Tip=TipEnabled?Attack.HitSpec.CanonicalBladeTipOffset:FVector::ZeroVector;
    Bytes<<TipEnabled<<Tip<<Read.Times;
    if (TargetSkeleton!=Skeleton)
    {
        // Append only for a distinct target: existing same-skeleton NPC stamps stay byte-identical.
        FString TargetPolicy=TEXT("ProjectJ.CanonicalMelee.CompatibleRawTarget.v2;NameMapRawKeys;TargetVirtualBonesThenBlendThenNativeRetargetThenRawRootLock"),TargetPath=TargetSkeleton->GetPathName();
        Bytes<<TargetPolicy<<TargetPath;
        if (!AppendReference(Bytes,TargetSkeleton->GetReferenceSkeleton(),Failure)) return false;
        int32 TargetBones=TargetSkeleton->GetReferenceSkeleton().GetNum();Bytes<<TargetBones;
        for (int32 Bone=0;Bone<TargetBones;++Bone)
        {int32 Mode=int32(TargetSkeleton->GetBoneTranslationRetargetingMode(Bone));Bytes<<Mode;}
        bool UseSourceModes=TargetSkeleton->GetUseRetargetModesFromCompatibleSkeleton();Bytes<<UseSourceModes;
        int32 TargetVirtuals=TargetSkeleton->GetVirtualBones().Num();Bytes<<TargetVirtuals;
        for (const auto& Virtual:TargetSkeleton->GetVirtualBones())
        {FString A=Virtual.SourceBoneName.ToString(),B=Virtual.TargetBoneName.ToString(),C=Virtual.VirtualBoneName.ToString();Bytes<<A<<B<<C;}
        for (const USkeleton* Rig:{Skeleton,TargetSkeleton})
        {
            TArray<FString> Compatible;
            for (const auto& Item:Rig->GetCompatibleSkeletons()) Compatible.Add(Item.ToSoftObjectPath().ToString());
            Compatible.Sort();Bytes<<Compatible;
        }
    }
    if (Bytes.IsError()) return Fail(TEXT("Source fingerprint serialization failed."));
    Read.MetadataStamp=LexToString(FBlake3::HashBuffer(Bytes.GetData(),Bytes.Num()));
    Out=MoveTemp(Read); return true;
}

bool BuildCompatibleRawKeys(const FCanonicalSource& Source,USkeletalMesh& Mesh,const FVector& Tip,
    TArray<FProject_JMeleeTrajectoryKey>& OutKeys,FString& Failure)
{
    const auto Fail=[&](const TCHAR* Why) {Failure=Why;return false;};
    FMemMark BuildMark(FMemStack::Get());
    const FReferenceSkeleton& MeshRef=Mesh.GetRefSkeleton();
    if (!MeshRef.GetNum() || MeshRef.GetNum()>MAX_uint16) return Fail(TEXT("Compatible target exceeds native bone-index capacity."));
    TArray<FBoneIndexType> Required;
    for (int32 Bone=0;Bone<MeshRef.GetNum();++Bone) Required.Add(FBoneIndexType(Bone));
    FBoneContainer TargetBones;
    TargetBones.InitializeTo(Required,UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),Mesh);
    TargetBones.SetUseRAWData(true);TargetBones.SetDisableRetargeting(false);
    const int32 TraceMeshIndex=TargetBones.GetPoseBoneIndexForBoneName(Source.TraceBone);
    if (TraceMeshIndex==INDEX_NONE) return Fail(TEXT("Compatible target is missing the selected trace bone."));
    const FCompactPoseBoneIndex TraceIndex=TargetBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(TraceMeshIndex));
    if (TraceIndex==INDEX_NONE) return Fail(TEXT("Compatible trace bone is absent from the native compact pose."));
    const FSkeletonRemapping& Remapping=UE::Anim::FSkeletonRemappingRegistry::Get().GetRemapping(Source.Skeleton,Source.TargetSkeleton);
    if (!Remapping.IsValid()) return Fail(TEXT("Native compatible-skeleton remapping is unavailable."));
    struct FTrackedBone {FName Name;int32 SourceIndex;FCompactPoseBoneIndex TargetIndex;};
    TArray<FTrackedBone> Tracked;
    TArray<FName> Names;Source.Sequence->GetDataModel()->GetBoneTrackNames(Names);
    for (const FName Name:Names)
    {
        const int32 SourceIndex=Source.Skeleton->GetReferenceSkeleton().FindBoneIndex(Name);
        if (SourceIndex==INDEX_NONE) return Fail(TEXT("RAW track is missing its source-skeleton bone."));
        const int32 TargetIndex=Remapping.GetTargetSkeletonBoneIndex(SourceIndex);
        if (TargetIndex==INDEX_NONE) continue; // Native compatible playback also skips absent target tracks.
        const FCompactPoseBoneIndex Compact=TargetBones.GetCompactPoseIndexFromSkeletonIndex(TargetIndex);
        if (Compact!=INDEX_NONE) Tracked.Add({Name,SourceIndex,Compact});
    }
    FAnimPoseEvaluationOptions RawOptions;RawOptions.EvaluationType=EAnimDataEvalType::Raw;
    RawOptions.bShouldRetarget=false;RawOptions.bExtractRootMotion=false;
    RawOptions.bIncorporateRootMotionIntoPose=true;RawOptions.bEvaluateCurves=true;
    // No OptionalSkeletalMesh: RAW extraction stays on its own source skeleton.
    // Native source-index addressing is valid here. No translation retarget or root lock has run yet.
    const auto PrepareKey=[&](double KeyTime,FCompactPose& Pose)
    {
        FAnimPose Raw;UAnimPoseExtensions::GetAnimPoseAtTime(Source.Sequence,KeyTime,RawOptions,Raw);
        if (!Raw.IsValid()) return Fail(TEXT("Source RAW key evaluation failed."));
        TArray<FName> Available;UAnimPoseExtensions::GetBoneNames(Raw,Available);
        Pose.SetBoneContainer(&TargetBones);Pose.ResetToRefPose();
        for (const FTrackedBone& Bone:Tracked)
        {
            if (!Available.Contains(Bone.Name)) return Fail(TEXT("Source RAW key is missing a tracked bone."));
            const FTransform Value=UAnimPoseExtensions::GetBonePose(Raw,Bone.Name,EAnimPoseSpaces::Local);
            if (Value.ContainsNaN()) return Fail(TEXT("Source RAW key contains a non-finite transform."));
            Pose[Bone.TargetIndex]=Value;
        }
        // Match ExtractPose: build untracked target virtual bones on each RAW key,
        // before interpolation and before the native retargeting scope's work.
        FCSPose<FCompactPose> RawCS;RawCS.InitPose(Pose);
        for (const FVirtualBoneCompactPoseData& Virtual:TargetBones.GetVirtualBoneCompactPoseData())
        {
            if (Tracked.ContainsByPredicate([&](const FTrackedBone& Bone){return Bone.TargetIndex==Virtual.VBIndex;})) continue;
            const FTransform From=RawCS.GetComponentSpaceTransform(Virtual.SourceIndex);
            const FTransform To=RawCS.GetComponentSpaceTransform(Virtual.TargetIndex);
            Pose[Virtual.VBIndex]=To.GetRelativeTransform(From);
        }
        return true;
    };
    const auto* Model=Source.Sequence->GetDataModel();
    const FFrameRate Rate=Model->GetFrameRate();
    if (Rate.Numerator<=0 || Rate.Denominator<=0 || Model->GetNumberOfKeys()<1) return Fail(TEXT("RAW source frame grid is invalid."));
    const TArray<FTransform> RetargetPose=Source.Sequence->GetRetargetTransforms();
    if (RetargetPose.IsEmpty()) return Fail(TEXT("Native source retarget reference pose is absent."));
    FTransform RawFirstRoot=TargetBones.GetRefPoseArray()[0];
    const FName SourceRoot=Source.Skeleton->GetReferenceSkeleton().GetBoneName(0);
    if (Model->IsValidBoneTrackName(SourceRoot))
        Source.Sequence->GetBoneTransform(RawFirstRoot,FSkeletonPoseBoneIndex(0),FAnimExtractContext(0.0),true);
    if (RawFirstRoot.ContainsNaN()) return Fail(TEXT("RAW first-frame root is not finite."));
    const FRootMotionReset RootReset(Source.Sequence->bEnableRootMotion,Source.Sequence->RootMotionRootLock,
        Source.Sequence->bForceRootLock,RawFirstRoot,false);
    TArray<FProject_JMeleeTrajectoryKey> Built;
    for (const float MontageTime:Source.Times)
    {
        FMemMark SampleMark(FMemStack::Get());
        const float Time=Source.Segment.ConvertTrackPosToAnimPos(MontageTime);
        if (!FMath::IsFinite(Time) || Time<0 || Time>Source.Sequence->GetPlayLength()) return Fail(TEXT("Mapped RAW source sample is out of bounds."));
        int32 First=0,Second=0;float Alpha=0;
        FAnimationRuntime::GetKeyIndicesFromTime(First,Second,Alpha,double(Time),Rate,Model->GetNumberOfKeys());
        if (Source.Sequence->Interpolation==EAnimInterpolationType::Step) Alpha=0;
        bool Interpolate=true;
        if (Alpha<UE_KINDA_SMALL_NUMBER) {Alpha=0;Interpolate=false;}
        else if (Alpha>1.f-UE_KINDA_SMALL_NUMBER) {First=Second;Interpolate=false;}
        FCompactPose Pose,Next;
        if (!PrepareKey(Rate.AsSeconds(FFrameTime(FFrameNumber(First))),Pose)) return false;
        if (Interpolate)
        {
            if (!PrepareKey(Rate.AsSeconds(FFrameTime(FFrameNumber(Second))),Next)) return false;
            for (const FCompactPoseBoneIndex Bone:Pose.ForEachBoneIndex()) Pose[Bone].Blend(Pose[Bone],Next[Bone],Alpha);
        }
        // Match FRetargetingScope's destructor: retarget only authored tracks,
        // once, after RAW-key interpolation. Native code owns the reference-pose
        // orientation conversion and source/target translation-retarget policies.
        for (const FTrackedBone& Bone:Tracked)
            FAnimationRuntime::RetargetBoneTransform(Source.Skeleton,Source.Sequence->GetRetargetTransformsSourceName(),RetargetPose,
                Pose[Bone.TargetIndex],Bone.SourceIndex,Bone.TargetIndex,TargetBones,false);
        // Match UAnimSequence after model evaluation: apply target root lock once.
        // AnimFirstFrame uses the RAW source first root, as native FRootMotionReset
        // does; RefPose uses the selected target mesh reference root.
        if (RootReset.bEnableRootMotion || RootReset.bForceRootLock)
            RootReset.ResetRootBoneForRootMotion(Pose[FCompactPoseBoneIndex(0)],TargetBones);
        FCSPose<FCompactPose> FinalCS;FinalCS.InitPose(Pose);
        const FTransform Trace=Source.SocketLocal*FinalCS.GetComponentSpaceTransform(TraceIndex);
        const FVector Position=Trace.TransformPosition(Tip);
        if (Trace.ContainsNaN() || !IsFiniteVector(Position)) return Fail(TEXT("Retargeted RAW trace is not finite."));
        FProject_JMeleeTrajectoryKey Key;Key.MontageTime=MontageTime;Key.MeshPosition=Position;Built.Add(Key);
    }
    OutKeys=MoveTemp(Built);return true;
}

FString MakeOutputStamp(const FCanonicalSource& Source,const FProject_JMeleeTrajectory& Path)
{
    FBufferArchive Bytes; FString Metadata=Source.MetadataStamp; Bytes<<Metadata;
    // Bind stored samples as well as input metadata; arbitrary key edits require an explicit re-bake.
    int32 Count=Path.Keys.Num(); Bytes<<Count;
    for (const auto& Key:Path.Keys) { float Time=Key.MontageTime;FVector Position=Key.MeshPosition;Bytes<<Time<<Position; }
    return LexToString(FBlake3::HashBuffer(Bytes.GetData(),Bytes.Num()));
}
}

bool Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(const UProject_JAttackDefinition& Attack,USkeletalMesh& Mesh,
    const FProject_JMeleeTrajectory& Path,FString& OutFailure)
{
    OutFailure=Path.GetValidationFailure(); if (!OutFailure.IsEmpty()) return false;
    if (!Path.bEnabled) return true;
    FCanonicalSource Source;
    if (!CaptureSource(Attack,Mesh,Source,OutFailure)) return false;
    if (Path.SourceMesh.ToSoftObjectPath()!=FSoftObjectPath(&Mesh) || Path.SourceMontage.ToSoftObjectPath()!=FSoftObjectPath(Source.Montage) ||
        Path.NotifyIndex!=Source.NotifyIndex || Path.SocketName!=Source.SocketName || Path.WindowStart!=Source.Start || Path.WindowEnd!=Source.End ||
        Path.bUseBladeTipOffset!=Attack.HitSpec.bUseCanonicalBladeTipOffset ||
        Path.BladeTipOffset!=(Path.bUseBladeTipOffset?Attack.HitSpec.CanonicalBladeTipOffset:FVector::ZeroVector) || Path.Keys.Num()!=Source.Times.Num())
    { OutFailure=TEXT("Canonical melee source binding or sampling grid changed; re-bake the selected attack."); return false; }
    for (int32 Index=0;Index<Source.Times.Num();++Index)
        if (Path.Keys[Index].MontageTime!=Source.Times[Index])
        { OutFailure=TEXT("Canonical melee sampling times changed; re-bake the selected attack."); return false; }
    if (Path.SourceStamp!=MakeOutputStamp(Source,Path))
    { OutFailure=TEXT("Canonical melee RAW source, retarget/root-lock inputs or stored keys changed; re-bake the selected attack."); return false; }
    return true;
}

bool Project_J::CanonicalMeleeAuthoring::BuildCandidate(const UProject_JAttackDefinition& Attack,USkeletalMesh& SourceMesh,
    FProject_JMeleeTrajectory& OutPath,FString& OutFailure)
{
    const auto Fail=[&](const TCHAR* Why) { OutFailure=Why;return false; };
    if (!IsInGameThread() || !IsValid(&Attack) || !IsValid(&SourceMesh) || (GEditor && GEditor->PlayWorld))
        return Fail(TEXT("RAW canonical producer requires selected live inputs on the editor GT outside PIE."));
    TStrongObjectPtr<UProject_JAttackDefinition> KeepAttack(const_cast<UProject_JAttackDefinition*>(&Attack));
    TStrongObjectPtr<USkeletalMesh> KeepMesh(&SourceMesh);
    FCanonicalSource Source;
    if (!CaptureSource(Attack,SourceMesh,Source,OutFailure)) return false;
    TStrongObjectPtr<UAnimMontage> KeepMontage(Source.Montage);TStrongObjectPtr<UAnimSequence> KeepSequence(Source.Sequence);TStrongObjectPtr<USkeleton> KeepSkeleton(Source.Skeleton);TStrongObjectPtr<USkeleton> KeepTargetSkeleton(Source.TargetSkeleton);
    FProject_JMeleeTrajectory Candidate;Candidate.bEnabled=true;Candidate.SourceMesh=&SourceMesh;Candidate.SourceMontage=Source.Montage;
    Candidate.NotifyIndex=Source.NotifyIndex;Candidate.SocketName=Source.SocketName;Candidate.bUseBladeTipOffset=Attack.HitSpec.bUseCanonicalBladeTipOffset;
    Candidate.BladeTipOffset=Candidate.bUseBladeTipOffset?Attack.HitSpec.CanonicalBladeTipOffset:FVector::ZeroVector;
    Candidate.WindowStart=Source.Start;Candidate.WindowEnd=Source.End;
    if (Source.TargetSkeleton!=Source.Skeleton)
    {
        if (!BuildCompatibleRawKeys(Source,SourceMesh,Candidate.BladeTipOffset,Candidate.Keys,OutFailure)) return false;
    }
    else
    {
    FAnimPoseEvaluationOptions Options;Options.EvaluationType=EAnimDataEvalType::Raw;Options.bShouldRetarget=true;Options.bExtractRootMotion=true;
    Options.bIncorporateRootMotionIntoPose=false;Options.OptionalSkeletalMesh=&SourceMesh;Options.bEvaluateCurves=true;
    for (const float MontageTime:Source.Times)
    {
        const float SequenceTime=Source.Segment.ConvertTrackPosToAnimPos(MontageTime);
        if (!FMath::IsFinite(SequenceTime) || SequenceTime<0 || SequenceTime>Source.Sequence->GetPlayLength()) return Fail(TEXT("Mapped sequence sample is outside the source."));
        FAnimPose Pose;UAnimPoseExtensions::GetAnimPoseAtTime(Source.Sequence,double(SequenceTime),Options,Pose);
        if (!Pose.IsValid()) return Fail(TEXT("RAW source evaluation failed; no gameplay path was written."));
        TArray<FName> PoseBones;UAnimPoseExtensions::GetBoneNames(Pose,PoseBones);
        if (!PoseBones.Contains(Source.TraceBone)) return Fail(TEXT("Evaluated pose does not contain the selected trace bone."));
        if (Source.bSocket)
        { TArray<FName> PoseSockets;UAnimPoseExtensions::GetSocketNames(Pose,PoseSockets);if (!PoseSockets.Contains(Source.SocketName)) return Fail(TEXT("Evaluated pose does not contain the selected trace socket.")); }
        // EAnimPoseSpaces::World is the pose's component/global space, not a live UWorld.
        const FTransform Trace=Source.bSocket?UAnimPoseExtensions::GetSocketPose(Pose,Source.SocketName,EAnimPoseSpaces::World):
            UAnimPoseExtensions::GetBonePose(Pose,Source.SocketName,EAnimPoseSpaces::World);
        const FVector Position=Trace.TransformPosition(Candidate.BladeTipOffset);
        if (Trace.ContainsNaN() || !IsFiniteVector(Position)) return Fail(TEXT("Evaluated gameplay sample is not finite."));
        FProject_JMeleeTrajectoryKey Key;Key.MontageTime=MontageTime;Key.MeshPosition=Position;Candidate.Keys.Add(Key);
    }
    }
    Candidate.SourceStamp=MakeOutputStamp(Source,Candidate);
    if (!Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(Attack,SourceMesh,Candidate,OutFailure)) return false;
    OutPath=MoveTemp(Candidate);OutFailure.Reset();return true;
}

FString UProject_JAssetMigrationLibrary::BakeCanonicalMeleeTrajectory(UProject_JAttackDefinition* Attack,USkeletalMesh* SourceMesh,bool bExecute,bool bSave)
{
    const auto Result=MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("schema"),1);Result->SetBoolField(TEXT("execute_requested"),bExecute);Result->SetBoolField(TEXT("save_requested"),bSave);
    Result->SetStringField(TEXT("sampling_policy"),SamplingPolicy);
    bool bPreflight=false,bExecuted=false,bSaved=false;FString Reason;
    const auto Finish=[&]()
    {
        Result->SetBoolField(TEXT("preflight_success"),bPreflight);Result->SetBoolField(TEXT("executed"),bExecuted);Result->SetBoolField(TEXT("saved"),bSaved);
        Result->SetStringField(TEXT("reason"),Reason);FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));return Json;
    };
    const auto Fail=[&](const TCHAR* Why) { Reason=Why;return Finish(); };
    if (!IsInGameThread() || !IsValid(Attack) || !IsValid(SourceMesh) || (GEditor && GEditor->PlayWorld) || (bSave && !bExecute))
        return Fail(TEXT("Require selected live assets on the editor GT outside PIE, and execute before save."));
    TStrongObjectPtr<UProject_JAttackDefinition> KeepAttack(Attack);TStrongObjectPtr<USkeletalMesh> KeepMesh(SourceMesh);
    UPackage* Package=Attack->GetOutermost();
    if (Package==GetTransientPackage() || !Attack->IsAsset() || Package->IsDirty() || !Package->IsFullyLoaded())
        return Fail(TEXT("Selected attack must be a fully loaded clean persistent asset; preserve unrelated unsaved edits."));
    const TStrongObjectPtr<UPackage> KeepPackage(Package);
    UObject* const TargetOuter=Attack->GetOuter();
    const TStrongObjectPtr<UObject> KeepOuter(TargetOuter);
    const FString TargetPath=Attack->GetPathName();
    const FProject_JMeleeTrajectory Original=Attack->MeleeTrajectory;
    const FString OtherFields=OtherAttackFields(*Attack);
    // Freeze the selected target before either compiler completion can invoke listeners.
    const auto IsTargetCurrent=[&]()
    {
        return IsValid(Attack) && !Attack->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed) && IsValid(Package) &&
            Attack->GetOuter()==TargetOuter && Attack->GetOutermost()==Package && Attack->GetPathName()==TargetPath &&
            Attack->IsAsset() && Package!=GetTransientPackage() && Package->IsFullyLoaded();
    };
    FCanonicalSource Source;
    if (!CaptureSource(*Attack,*SourceMesh,Source,Reason)) return Finish();
    if (!IsTargetCurrent() || Package->IsDirty() || !SamePath(Attack->MeleeTrajectory,Original) || OtherAttackFields(*Attack)!=OtherFields)
        return Fail(TEXT("Selected attack identity, clean state or inputs changed during source preparation."));
    TStrongObjectPtr<UAnimMontage> KeepMontage(Source.Montage);TStrongObjectPtr<UAnimSequence> KeepSequence(Source.Sequence);TStrongObjectPtr<USkeleton> KeepSkeleton(Source.Skeleton);TStrongObjectPtr<USkeleton> KeepTargetSkeleton(Source.TargetSkeleton);
    Result->SetStringField(TEXT("attack"),Attack->GetPathName());Result->SetStringField(TEXT("source_mesh"),SourceMesh->GetPathName());
    Result->SetStringField(TEXT("source_montage"),Source.Montage->GetPathName());Result->SetStringField(TEXT("source_sequence"),Source.Sequence->GetPathName());
    FProject_JMeleeTrajectory Candidate;
    if (!Project_J::CanonicalMeleeAuthoring::BuildCandidate(*Attack,*SourceMesh,Candidate,Reason)) return Finish();
    if (!IsTargetCurrent() || Package->IsDirty() || !SamePath(Attack->MeleeTrajectory,Original) || OtherAttackFields(*Attack)!=OtherFields)
        return Fail(TEXT("Selected attack identity, clean state or inputs changed during RAW evaluation."));
    Result->SetStringField(TEXT("source_stamp"),Candidate.SourceStamp);Result->SetNumberField(TEXT("key_count"),Candidate.Keys.Num());
    Result->SetNumberField(TEXT("window_start"),Candidate.WindowStart);Result->SetNumberField(TEXT("window_end"),Candidate.WindowEnd);
    TArray<TSharedPtr<FJsonValue>> Samples;
    for (const auto& Key:Candidate.Keys)
    { const auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("montage_time"),Key.MontageTime);Row->SetNumberField(TEXT("x"),Key.MeshPosition.X);Row->SetNumberField(TEXT("y"),Key.MeshPosition.Y);Row->SetNumberField(TEXT("z"),Key.MeshPosition.Z);Samples.Add(MakeShared<FJsonValueObject>(Row)); }
    Result->SetArrayField(TEXT("keys"),Samples);bPreflight=true;
    if (!bExecute) return Finish();
    FScopedTransaction Transaction(NSLOCTEXT("ProjectJCanonicalMelee","Bake","Bake canonical melee gameplay trajectory"),!IsRunningCommandlet());
    if (!IsTargetCurrent() || Package->IsDirty() || !SamePath(Attack->MeleeTrajectory,Original) || OtherAttackFields(*Attack)!=OtherFields)
    { Transaction.Cancel();return Fail(TEXT("Selected attack changed while opening the editor transaction.")); }
    Attack->Modify();
    // Validate sources first: FinishCompilation can itself edit/rename the target.
    if (!Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*Attack,*SourceMesh,Candidate,Reason) ||
        !IsTargetCurrent() || !SamePath(Attack->MeleeTrajectory,Original) || OtherAttackFields(*Attack)!=OtherFields)
    {
        // Modify may already have dirtied the package. Do not clear a listener's unrelated dirty edit.
        Transaction.Cancel();if (Reason.IsEmpty()) Reason=TEXT("Selected attack changed before installation.");return Finish();
    }
    Attack->MeleeTrajectory=Candidate;
    const auto Rollback=[&]()
    {
        // Retire only this writer's exact bundle. Never overwrite reentrant unrelated edits.
        const bool bOwns=IsValid(Attack) && SamePath(Attack->MeleeTrajectory,Candidate);
        if (bOwns)
        {
            Attack->MeleeTrajectory=Original;
            // The object may have moved. Preserve every package's dirty state;
            // this writer's restored field still needs review after a callback/save failure.
            Attack->GetOutermost()->SetDirtyFlag(true);
        }
        Result->SetBoolField(TEXT("own_field_rolled_back"),bOwns);Transaction.Cancel();
    };
    Attack->PostEditChange();
    if (!IsTargetCurrent())
    { Rollback();return Fail(TEXT("Selected attack moved or retired during PostEditChange.")); }
    Attack->MarkPackageDirty();
    if (!IsTargetCurrent())
    { Rollback();return Fail(TEXT("Selected attack moved or retired while marking its package dirty.")); }
    FDataValidationContext Context;
    const EDataValidationResult Validation=Attack->IsDataValid(Context);
    if (Validation==EDataValidationResult::Invalid ||
        !Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*Attack,*SourceMesh,Candidate,Reason) ||
        !IsTargetCurrent() || !SamePath(Attack->MeleeTrajectory,Candidate) || OtherAttackFields(*Attack)!=OtherFields)
    { Rollback();if (Reason.IsEmpty()) Reason=TEXT("Installed attack failed validation or changed during editor callbacks.");return Finish(); }
    if (bSave)
    {
        const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if (!UPackage::SavePackage(Package,Attack,*Filename,Args))
        { Rollback();return Fail(TEXT("Selected package save failed; own in-memory field rolled back, disk write success is not claimed.")); }
        bSaved=true;
        if (!Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*Attack,*SourceMesh,Candidate,Reason) ||
            !IsTargetCurrent() || !SamePath(Attack->MeleeTrajectory,Candidate) || OtherAttackFields(*Attack)!=OtherFields)
        {
            Rollback();
            // A completed SavePackage is an observed write. Do not claim a disk rollback after external save callbacks.
            if (Reason.IsEmpty()) Reason=TEXT("Selected package was written but editor save callbacks changed the validated inputs; disk rollback is not claimed.");
            return Finish();
        }
    }
    bExecuted=true;return Finish();
}
