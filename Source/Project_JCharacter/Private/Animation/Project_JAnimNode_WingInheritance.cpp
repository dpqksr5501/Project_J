#include "Animation/Project_JAnimNode_WingInheritance.h"

#include "Animation/AnimInstanceProxy.h"

void FProject_JAnimNode_WingInheritance::InitializeBoneReferences(const FBoneContainer& Bones)
{
    bValidChain = false;
    bHasHistory = false;
    PendingSeconds = 0.f;

    const auto Find = [&Bones](FName Name)
    {
        FBoneReference Reference(Name);
        Reference.Initialize(Bones);
        return Reference.GetCompactPoseIndex(Bones);
    };
    Root = Find(TEXT("root"));
    Torso = Find(TEXT("spine_04"));
    if (Root.GetInt() == INDEX_NONE || Torso.GetInt() == INDEX_NONE)
    {
        return;
    }

    static const FName Names[2][NumStages] = {
        {TEXT("a_01"), TEXT("L_01"), TEXT("L_02"), TEXT("L_03"), TEXT("L_04")},
        {TEXT("a_01"), TEXT("R_01"), TEXT("R_02"), TEXT("R_03"), TEXT("R_04")}
    };
    for (int32 Side = 0; Side < 2; ++Side)
    {
        FCompactPoseBoneIndex Parent = Torso;
        for (int32 Stage = 0; Stage < NumStages; ++Stage)
        {
            const FCompactPoseBoneIndex Bone = Find(Names[Side][Stage]);
            if (Bone.GetInt() == INDEX_NONE || Bones.GetParentBoneIndex(Bone) != Parent)
            {
                return;
            }
            WingChains[Side][Stage] = Bone;
            Parent = Bone;
        }
    }

    FQuat ReferenceTorso = FQuat::Identity;
    FCompactPoseBoneIndex Bone = Torso;
    for (; Bone != Root && Bone.GetInt() != INDEX_NONE; Bone = Bones.GetParentBoneIndex(Bone))
    {
        ReferenceTorso = Bones.GetRefPoseTransform(Bone).GetRotation() * ReferenceTorso;
    }
    InverseReferenceTorso = ReferenceTorso.GetNormalized().Inverse();
    bValidChain = Bone == Root;
}

void FProject_JAnimNode_WingInheritance::UpdateInternal(const FAnimationUpdateContext& Context)
{
    FAnimNode_SkeletalControlBase::UpdateInternal(Context);
    PendingSeconds += Context.GetDeltaTime();
}

void FProject_JAnimNode_WingInheritance::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
    TArray<FBoneTransform>& OutBoneTransforms)
{
    const FQuat RootRotation = Output.Pose.GetComponentSpaceTransform(Root).GetRotation();
    const FQuat InverseRoot = RootRotation.Inverse();
    const FQuat TorsoRotation = Output.Pose.GetComponentSpaceTransform(Torso).GetRotation();
    // The wing clip owns the fold. Only the torso's deviation from its root-relative reference is inherited.
    const FRotator Delta = (InverseRoot * TorsoRotation * InverseReferenceTorso).Rotator();
    const auto ClampAxis = [](double Value, double Limit)
    {
        return FMath::Clamp(Value, -FMath::Abs(Limit), FMath::Abs(Limit));
    };
    const FQuat Target = FRotator(ClampAxis(Delta.Pitch, MaxTorsoRotation.Pitch),
        ClampAxis(Delta.Yaw, MaxTorsoRotation.Yaw), ClampAxis(Delta.Roll, MaxTorsoRotation.Roll)).Quaternion();

    const bool bFullDetail = Output.AnimInstanceProxy->GetLODLevel() == 0;
    if (!bHasHistory || PendingSeconds > 0.25f)
    {
        MidRotation = TipRotation = Target;
        bHasHistory = true;
    }
    else
    {
        const auto Follow = [this, &Target](const FQuat& Previous, float ResponseTime)
        {
            const float Weight = 1.f - FMath::Exp(-PendingSeconds / FMath::Max(ResponseTime, 0.001f));
            return FQuat::FastLerp(Previous, Target, Weight).GetNormalized();
        };
        MidRotation = Follow(MidRotation, MidResponseTime);
        TipRotation = bFullDetail ? Follow(TipRotation, TipResponseTime) : MidRotation;
    }
    PendingSeconds = 0.f;

    // Each stage specifies total inheritance in root space, so parent rotation does not accumulate at the tips.
    static constexpr double Gains[NumStages] = {0.72, 0.66, 0.48, 0.29, 0.14};
    const FQuat Samples[NumStages] = {Target, Target, MidRotation, MidRotation, TipRotation};
    FQuat ComponentDeltas[NumStages];
    for (int32 Stage = 0; Stage < NumStages; ++Stage)
    {
        const FQuat Weighted = FQuat::FastLerp(FQuat::Identity, Samples[Stage], Gains[Stage]).GetNormalized();
        ComponentDeltas[Stage] = RootRotation * Weighted * InverseRoot;
    }

    const FTransform Anchor = Output.Pose.GetComponentSpaceTransform(WingChains[0][0]);
    FTransform AdjustedAnchor = Anchor;
    AdjustedAnchor.SetRotation((ComponentDeltas[0] * Anchor.GetRotation()).GetNormalized());
    OutBoneTransforms.Emplace(WingChains[0][0], AdjustedAnchor);
    for (const auto& Chain : WingChains)
    {
        FVector OriginalParent = Anchor.GetTranslation();
        FVector AdjustedParent = OriginalParent;
        for (int32 Stage = 1; Stage < NumStages; ++Stage)
        {
            FTransform Joint = Output.Pose.GetComponentSpaceTransform(Chain[Stage]);
            const FVector OriginalPosition = Joint.GetTranslation();
            // Keep the authored parent-relative offset and rotate it about the corrected parent pivot.
            AdjustedParent += ComponentDeltas[Stage - 1].RotateVector(OriginalPosition - OriginalParent);
            OriginalParent = OriginalPosition;
            Joint.SetTranslation(AdjustedParent);
            Joint.SetRotation((ComponentDeltas[Stage] * Joint.GetRotation()).GetNormalized());
            OutBoneTransforms.Emplace(Chain[Stage], Joint);
        }
    }
    OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
