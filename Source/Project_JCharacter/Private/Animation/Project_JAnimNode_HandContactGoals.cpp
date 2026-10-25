#include "Animation/Project_JAnimNode_HandContactGoals.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"

void FProject_JAnimNode_HandContactGoals::PreUpdate(const UAnimInstance* Instance)
{
	Hands[0] = Hands[1] = NAME_None;
	if (const auto* Body=Cast<UProject_JRetargetAnimInstance>(Instance))
	{
		Hands[0]=Body->GetBodyHandContact(true).Hand;
		Hands[1]=Body->GetBodyHandContact(false).Hand;
	}
}

void FProject_JAnimNode_HandContactGoals::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	Targets[0]=Targets[1]={};bFollowPublishedPrimary=false;bBonesValid=false;
}

void FProject_JAnimNode_HandContactGoals::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	Targets[0]=Targets[1]={};bFollowPublishedPrimary=false;
	const auto* Body=Cast<UProject_JRetargetAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject());
	if (!Body) return;
	Targets[0]={Body->GetInputPoseContact(true),Body->RightWristTarget,Body->bRightContactTargetValid?Body->RightGripAlpha:0.f};
	Targets[1]={Body->GetInputPoseContact(false),Body->LeftWristTarget,Body->bLeftContactTargetValid?Body->LeftGripAlpha:0.f};
	if (Body->bUsePrimaryHandSpaceGrip && Body->PrimaryHandBoneName==Hands[0])
	{
		Targets[1].Relative={Hands[0],Body->LeftGripInPrimaryHandSpace};
		bFollowPublishedPrimary=true;
	}
	for (auto& Target:Targets)
		Target.Alpha=FMath::IsFinite(Target.Alpha)?FMath::Clamp(Target.Alpha,0.f,1.f):0.f;
}

FName FProject_JAnimNode_HandContactGoals::ReferenceName(int32 Side) const
{
	return Targets[Side].Alpha>UE_SMALL_NUMBER && !(Side==1 && bFollowPublishedPrimary) && Targets[Side].Relative.IsValid()
		? Targets[Side].Relative.Bone : NAME_None;
}

void FProject_JAnimNode_HandContactGoals::InitializeBoneReferences(const FBoneContainer& Bones)
{
	bBonesValid=true;
	auto Resolve=[&Bones](FName Name)
	{
		FBoneReference Ref;Ref.BoneName=Name;Ref.Initialize(Bones);
		return Ref.IsValidToEvaluate(Bones)?Ref.GetCompactPoseIndex(Bones):FCompactPoseBoneIndex(INDEX_NONE);
	};
	for (int32 Side=0;Side<2;++Side)
	{
		CachedHands[Side]=Hands[Side];CachedGoals[Side]=Side==0?PrimaryGoalBone.BoneName:SecondaryGoalBone.BoneName;
		CachedReferences[Side]=ReferenceName(Side);
		HandIndices[Side]=Resolve(CachedHands[Side]);GoalIndices[Side]=Resolve(CachedGoals[Side]);
		ReferenceIndices[Side]=CachedReferences[Side].IsNone()?FCompactPoseBoneIndex(INDEX_NONE):Resolve(CachedReferences[Side]);
		bBonesValid&=HandIndices[Side]!=INDEX_NONE && GoalIndices[Side]!=INDEX_NONE &&
			(CachedReferences[Side].IsNone() || ReferenceIndices[Side]!=INDEX_NONE);
	}
	bBonesValid&=GoalIndices[0]!=GoalIndices[1];
	if (!bBonesValid) return;
	// Goal markers cannot be ancestors of either body hand. Publishing them must
	// never alter the input body or the reference from which another goal is read.
	for (int32 Side=0;Side<2;++Side) for (int32 Other=0;Other<2;++Other)
	{
		for (auto Index=HandIndices[Other];Index!=INDEX_NONE;Index=Bones.GetParentBoneIndex(Index))
			if (Index==GoalIndices[Side]) bBonesValid=false;
		for (auto Index=ReferenceIndices[Other];Index!=INDEX_NONE;Index=Bones.GetParentBoneIndex(Index))
			if (Index==GoalIndices[Side]) bBonesValid=false;
	}
}

bool FProject_JAnimNode_HandContactGoals::IsValidToEvaluate(const USkeleton*,const FBoneContainer& Bones)
{
	if (CachedHands[0]!=Hands[0] || CachedHands[1]!=Hands[1] || CachedGoals[0]!=PrimaryGoalBone.BoneName ||
		CachedGoals[1]!=SecondaryGoalBone.BoneName || CachedReferences[0]!=ReferenceName(0) || CachedReferences[1]!=ReferenceName(1))
		InitializeBoneReferences(Bones);
	return bBonesValid;
}

void FProject_JAnimNode_HandContactGoals::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Transforms)
{
	if (!bBonesValid) return;
	FTransform Published[2];
	for (int32 Side=0;Side<2;++Side)
	{
		const FTransform Input=Output.Pose.GetComponentSpaceTransform(HandIndices[Side]);
		FTransform Target=Targets[Side].Component;
		if (Targets[Side].Relative.IsValid())
		{
			if (Side==1 && bFollowPublishedPrimary) Target=Targets[Side].Relative.Wrist*Published[0];
			else if (ReferenceIndices[Side]!=INDEX_NONE)
				Target=Targets[Side].Relative.Wrist*Output.Pose.GetComponentSpaceTransform(ReferenceIndices[Side]);
		}
		Published[Side]=Input;
		if (Targets[Side].Alpha>UE_SMALL_NUMBER && !Target.ContainsNaN())
			Published[Side].Blend(Input,Target,Targets[Side].Alpha);
		Transforms.Emplace(GoalIndices[Side],Published[Side]);
	}
	Transforms.Sort([](const FBoneTransform& A,const FBoneTransform& B){return A.BoneIndex<B.BoneIndex;});
}
