#include "Animation/AnimNode_ProjectJStrafeUpperBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstanceProxy.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Algo/Reverse.h"

namespace
{
bool Heading(const FQuat& Pose, const FQuat& Ref, const FQuat& Component, const FVector& NeutralForward, float& OutYaw)
{
	if (Pose.ContainsNaN() || Ref.ContainsNaN() || Component.ContainsNaN() ||
		!Pose.IsNormalized() || !Ref.IsNormalized() || !Component.IsNormalized()) return false;
	FVector Forward = Component.RotateVector((Pose * Ref.Inverse()).RotateVector(NeutralForward));
	Forward.Z = 0;
	if (Forward.ContainsNaN() || Forward.IsNearlyZero()) return false;
	OutYaw = Forward.Rotation().Yaw;
	return FMath::IsFinite(OutYaw);
}
}

FAnimNode_ProjectJStrafeUpperBodyFacing::FAnimNode_ProjectJStrafeUpperBodyFacing()
{
	SpineRoot.BoneName = TEXT("spine_01");
	Chest.BoneName = TEXT("spine_05");
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::ResetDynamics(ETeleportType)
{
	Correction = FacingError = DesiredYaw = InputChestYaw = RootWeight = UprightWeight = PendingDeltaTime = 0.f;
	LastUpdate.Reset();
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
	Input = {};
	CachedRoot = CachedSpineRoot = CachedChest = NAME_None;
	Ancestors.Reset();
	SpineStart = RootPosition = INDEX_NONE;
	ResetDynamics(ETeleportType::ResetPhysics);
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	CachedRoot = CachedSpineRoot = CachedChest = NAME_None;
	Ancestors.Reset();
	SpineStart = RootPosition = INDEX_NONE;
	ResetDynamics(ETeleportType::ResetPhysics);
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::ResolveBones(const FBoneContainer& Bones)
{
	Ancestors.Reset();
	SpineStart = RootPosition = INDEX_NONE;
	CachedSpineRoot = bUseProfileBones ? Input.Policy.SpineRootBone : SpineRoot.BoneName;
	CachedChest = bUseProfileBones ? Input.Policy.ChestBone : Chest.BoneName;
	CachedRoot = bUseProfileBones ? Input.Policy.RootBone : FName(TEXT("root"));
	FBoneReference Lower, Upper, Root;
	Lower.BoneName = CachedSpineRoot; Upper.BoneName = CachedChest; Root.BoneName = CachedRoot;
	Lower.Initialize(Bones); Upper.Initialize(Bones); Root.Initialize(Bones);
	if (!Lower.IsValidToEvaluate(Bones) || !Upper.IsValidToEvaluate(Bones) || !Root.IsValidToEvaluate(Bones)) return;
	for (auto Index = Upper.GetCompactPoseIndex(Bones); Index != INDEX_NONE && Ancestors.Num() < 32; Index = Bones.GetParentBoneIndex(Index))
		Ancestors.Add(Index);
	Algo::Reverse(Ancestors);
	SpineStart = Ancestors.Find(Lower.GetCompactPoseIndex(Bones));
	RootPosition = Ancestors.Find(Root.GetCompactPoseIndex(Bones));
	if (RootPosition == INDEX_NONE || SpineStart <= RootPosition || Ancestors.Num() - SpineStart < 2 ||
		Bones.GetParentBoneIndex(Ancestors[0]) != INDEX_NONE)
	{
		Ancestors.Reset(); SpineStart = RootPosition = INDEX_NONE; return;
	}
	FTransform Reference = FTransform::Identity;
	for (int32 Index = 0; Index < Ancestors.Num(); ++Index)
	{
		Reference = Bones.GetRefPoseTransform(Ancestors[Index]) * Reference;
		if (Index == RootPosition) ReferenceRoot = Reference.GetRotation();
	}
	ReferenceChest = Reference.GetRotation();
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
	const auto& Counter = Context.AnimInstanceProxy->GetUpdateCounter();
	if (!LastUpdate.WasSynchronizedCounter(Counter) || !LastUpdate.WasSynchronizedLastFrame(Counter))
		Correction = PendingDeltaTime = 0.f;
	if (!LastUpdate.IsSynchronized_All(Counter))
	{
		PendingDeltaTime += Context.GetDeltaTime();
		LastUpdate.SynchronizeWith(Counter);
	}
	Input = {};
	if (const auto* Anim = Cast<UProject_JCharacterAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject()))
	{
		Input = Anim->GetThreadSafeStrafeUpperBodyFacing();
		const auto* Proxy = static_cast<const FProject_JCharacterAnimInstanceProxy*>(Context.AnimInstanceProxy);
		if (const auto* MM = Proxy->GetCapturedMotionMatchingNode())
			if (MM->GetMotionMatchingState().SearchResult.bIsInteraction) Input = {};
	}
	if (LODThreshold >= 0 && Context.AnimInstanceProxy->GetLODLevel() > LODThreshold) Input = {};
	if (!Input.bCanRelease) { Correction = PendingDeltaTime = 0.f; return; }
	const FName Lower = bUseProfileBones ? Input.Policy.SpineRootBone : SpineRoot.BoneName;
	const FName Upper = bUseProfileBones ? Input.Policy.ChestBone : Chest.BoneName;
	const FName Root = bUseProfileBones ? Input.Policy.RootBone : FName(TEXT("root"));
	if (Root != CachedRoot || Lower != CachedSpineRoot || Upper != CachedChest)
	{
		ResolveBones(Context.AnimInstanceProxy->GetRequiredBones());
		Correction = 0.f;
	}
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	const float Dt = PendingDeltaTime;
	PendingDeltaTime = 0.f;
	FacingError = DesiredYaw = RootWeight = UprightWeight = InputChestYaw = 0.f;
	if (!Input.bCanRelease || Ancestors.IsEmpty()) { Correction = 0.f; return; }
	if (!Input.bEnabled && FMath::Abs(Correction) < .001f) { Correction = 0.f; return; }
	// Only the small root-to-chest path is composed. No whole-pose component
	// conversion, per-character graph, search or UObject access in evaluation.
	TArray<FTransform, TInlineAllocator<32>> InputCS;
	FTransform Parent = FTransform::Identity;
	for (const auto Index : Ancestors)
	{
		const FTransform& Local = Output.Pose[Index];
		if (Local.ContainsNaN() || !Local.GetRotation().IsNormalized()) { Correction = 0.f; return; }
		Parent = Local * Parent;
		InputCS.Add(Parent);
	}
	const FQuat Component = Output.AnimInstanceProxy->GetComponentTransform().GetRotation();
	const FVector Neutral = Component.UnrotateVector(FRotator(0, Input.ActorYaw, 0).Vector());
	float RootYaw = 0.f;
	if (!Project_J::Animation::TryGetUpperBodyYaw(InputCS.Last().GetRotation(), ReferenceChest, Component, Neutral, InputChestYaw, UprightWeight) ||
		!Heading(InputCS[RootPosition].GetRotation(), ReferenceRoot, Component, Neutral, RootYaw))
	{
		Correction = 0.f; return;
	}
	FacingError = FMath::FindDeltaAngleDegrees(InputChestYaw, Input.TargetYaw);
	RootWeight = Project_J::Animation::UpperBodyRootFacingWeight(FMath::FindDeltaAngleDegrees(RootYaw, Input.TargetYaw), Input.Policy);
	DesiredYaw = Input.bEnabled ? Project_J::Animation::UpperBodyFacingDemand(FacingError, Input.Strength * RootWeight * UprightWeight, Input.Policy) : 0.f;
	Correction = Project_J::Animation::StepUpperBodyFacing(Correction, DesiredYaw, Dt, Input.Policy);
	if (FMath::Abs(Correction) < .001f) return;
	const FVector Axis = Component.UnrotateVector(FVector::UpVector).GetSafeNormal();
	const float Share = FMath::DegreesToRadians(Correction / (Ancestors.Num() - SpineStart));
	Parent = InputCS[SpineStart - 1];
	for (int32 Index = SpineStart; Index < Ancestors.Num(); ++Index)
	{
		FTransform& Local = Output.Pose[Ancestors[Index]];
		const FVector ParentAxis = Parent.GetRotation().UnrotateVector(Axis);
		Local.SetRotation((FQuat(ParentAxis, Share) * Local.GetRotation()).GetNormalized());
		Parent = Local * Parent;
	}
}

void FAnimNode_ProjectJStrafeUpperBodyFacing::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s Enabled=%d Error=%.2f Desired=%.2f Applied=%.2f RootWeight=%.2f Bones=%d"),
		*DebugData.GetNodeName(this), Input.bEnabled, FacingError, DesiredYaw, Correction, RootWeight, Ancestors.Num()));
	Source.GatherDebugData(DebugData);
}
