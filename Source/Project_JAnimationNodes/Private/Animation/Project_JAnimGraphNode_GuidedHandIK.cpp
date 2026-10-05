#include "Animation/Project_JAnimGraphNode_GuidedHandIK.h"
#include "Animation/Skeleton.h"
#include "Kismet2/CompilerResultsLog.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "ProjectJGuidedHandIK"

FText UProject_JAnimGraphNode_GuidedHandIK::GetControllerDescription() const
{
	return LOCTEXT("Title", "Project J Guided Hand IK");
}

FText UProject_JAnimGraphNode_GuidedHandIK::GetNodeTitle(ENodeTitleType::Type) const
{
	return GetControllerDescription();
}

FText UProject_JAnimGraphNode_GuidedHandIK::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Corrects a hand to an already calibrated WRIST target. Defaults to component space; optional bone-space contact uses the current input pose's independent reference bone, after preceding arm controls. Uses stable input-pose elbow guidance and actual limb lengths. Read arm roles/stability from the body profile, or set static bones. Explicit upper/forearm roles support intervening helpers while preserving their input segment-relative pose. Empty roles use immediate parents. No stretching or separate recovery clock.");
}

void UProject_JAnimGraphNode_GuidedHandIK::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
	// Profile modes resolve their actual skeleton/roles at runtime; static defaults cannot validate them.
	if (!ForSkeleton || Node.ArmDefinitionSource != EProject_JGuidedArmDefinitionSource::NodeSettings) { return; }
	const FReferenceSkeleton& Ref = ForSkeleton->GetReferenceSkeleton();
	if (Ref.GetNum() < 3) { MessageLog.Error(TEXT("@@: Guided Hand IK requires a skeleton with an arm chain."), this); return; }
	TArray<FBoneIndexType> Required;
	for (int32 Index = 0; Index < Ref.GetNum(); ++Index) { Required.Add(Index); }
	FBoneContainer Bones;
	Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *ForSkeleton);
	Project_J::Animation::FGuidedArmChain Chain;
	if (!Project_J::Animation::ResolveGuidedArmChain(Bones, Node.HandBone.BoneName, Node.ForearmBone.BoneName, Node.UpperArmBone.BoneName, Chain))
	{
		MessageLog.Error(TEXT("@@: Guided Hand IK requires a valid upper/forearm/hand chain; set both explicit roles or leave both empty."), this);
		return;
	}
	for (const auto Bone : Chain.Path)
	{
		const FVector Scale = Ref.GetRefBonePose()[Bones.GetSkeletonIndex(Bone)].GetScale3D();
		if (Scale.ContainsNaN() || Scale.X <= UE_SMALL_NUMBER || !Scale.Equals(FVector(Scale.X), UE_KINDA_SMALL_NUMBER))
		{
			MessageLog.Error(TEXT("@@: Guided Hand IK does not support mirrored or nonuniform arm reference scale."), this);
			break;
		}
	}
	const auto* SpacePin = FindPin(TEXT("EffectorSpaceBoneName"));
	const auto* UsePin = FindPin(TEXT("bUseBoneSpaceEffector"));
	if (Node.bUseBoneSpaceEffector && (!SpacePin || SpacePin->LinkedTo.IsEmpty()) && (!UsePin || UsePin->LinkedTo.IsEmpty()))
	{
		int32 Index = Ref.FindBoneIndex(Node.EffectorSpaceBoneName);
		const int32 Upper = Bones.GetSkeletonIndex(Chain.Upper);
		if (Index == INDEX_NONE) { MessageLog.Error(TEXT("@@: Guided Hand IK effector reference bone is missing."), this); }
		for (; Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
		{
			if (Index == Upper)
			{
				MessageLog.Error(TEXT("@@: Guided Hand IK effector reference cannot depend on this solved arm's subtree."), this);
				break;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
