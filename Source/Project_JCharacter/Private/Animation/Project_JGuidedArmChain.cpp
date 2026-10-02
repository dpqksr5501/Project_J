#include "Animation/Project_JGuidedArmChain.h"
#include "Algo/Reverse.h"

bool Project_J::Animation::ResolveGuidedArmChain(const FBoneContainer& Bones, FName HandName,
	FName ForearmName, FName UpperName, FGuidedArmChain& OutChain)
{
	OutChain = {};
	const auto Resolve = [&Bones](FName Name)
	{
		if (Name.IsNone()) { return FCompactPoseBoneIndex(INDEX_NONE); }
		FBoneReference Reference;
		Reference.BoneName = Name;
		Reference.Initialize(Bones);
		return Reference.IsValidToEvaluate(Bones) ? Reference.GetCompactPoseIndex(Bones) : FCompactPoseBoneIndex(INDEX_NONE);
	};
	OutChain.Hand = Resolve(HandName);
	if (OutChain.Hand == INDEX_NONE || ForearmName.IsNone() != UpperName.IsNone()) { return false; }
	if (ForearmName.IsNone())
	{
		OutChain.Forearm = Bones.GetParentBoneIndex(OutChain.Hand);
		if (OutChain.Forearm != INDEX_NONE) { OutChain.Upper = Bones.GetParentBoneIndex(OutChain.Forearm); }
	}
	else
	{
		OutChain.Forearm = Resolve(ForearmName);
		OutChain.Upper = Resolve(UpperName);
	}
	if (OutChain.Upper == INDEX_NONE || OutChain.Forearm == INDEX_NONE ||
		OutChain.Upper == OutChain.Forearm || OutChain.Hand == OutChain.Forearm || OutChain.Hand == OutChain.Upper) { return false; }
	for (FCompactPoseBoneIndex Index = OutChain.Hand; Index != INDEX_NONE; Index = Bones.GetParentBoneIndex(Index))
	{
		OutChain.Path.Add(Index);
		if (Index == OutChain.Upper) { break; }
	}
	if (OutChain.Path.Last() != OutChain.Upper) { OutChain.Path.Reset(); return false; }
	Algo::Reverse(OutChain.Path);
	OutChain.ForearmPathIndex = OutChain.Path.Find(OutChain.Forearm);
	if (!OutChain.IsValid()) { OutChain.Path.Reset(); return false; }
	return true;
}

bool Project_J::Animation::AppendGuidedArmTransforms(const FGuidedArmChain& Chain,
	FCSPose<FCompactPose>& Pose, const FTransform* InputArm, const FTransform* SolvedArm,
	TArray<FBoneTransform>& OutTransforms)
{
	if (!Chain.IsValid()) { return false; }
	const int32 Start = OutTransforms.Num();
	for (int32 I = 0; I < Chain.Path.Num(); ++I)
	{
		FTransform Result;
		if (I == 0) { Result = SolvedArm[0]; }
		else if (I == Chain.ForearmPathIndex) { Result = SolvedArm[1]; }
		else if (I == Chain.Path.Num() - 1) { Result = SolvedArm[2]; }
		else
		{
			const int32 Segment = I < Chain.ForearmPathIndex ? 0 : 1;
			Result = Pose.GetComponentSpaceTransform(Chain.Path[I]).GetRelativeTransform(InputArm[Segment]) * SolvedArm[Segment];
		}
		Result.NormalizeRotation();
		if (Result.ContainsNaN()) { OutTransforms.SetNum(Start); return false; }
		OutTransforms.Add(FBoneTransform(Chain.Path[I], Result));
	}
	// A hierarchy's compact indices are parent-before-child, as required by the
	// skeletal control's single LocalBlendCSBoneTransforms alpha application.
	return true;
}
