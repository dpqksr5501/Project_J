#include "Animation/AnimNode_ProjectJStrafeLowerBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstanceProxy.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Algo/Reverse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <limits>

namespace
{
TArray<FName, TInlineAllocator<9>> Roles(const FProject_JStrafeLowerBodyFacingPolicy& P)
{
	return {P.RootBone, P.PelvisBone, P.SpineRootBone, P.LeftThighBone, P.LeftCalfBone, P.LeftFootBone,
		P.RightThighBone, P.RightCalfBone, P.RightFootBone};
}
bool Valid(const FTransform& T)
{
	const FVector S = T.GetScale3D();
	return !T.ContainsNaN() && T.GetRotation().IsNormalized() && S.GetMin() > UE_SMALL_NUMBER && S.Equals(FVector(S.X), .001);
}
float Heading(const FQuat& Pose, const FQuat& Reference, const FQuat& Component, const FVector& Neutral)
{
	FVector V = Component.RotateVector((Pose * Reference.Inverse()).RotateVector(Neutral));
	V.Z = 0;
	return !V.ContainsNaN() && !V.IsNearlyZero() ? V.Rotation().Yaw : std::numeric_limits<float>::quiet_NaN();
}
}

void FAnimNode_ProjectJStrafeLowerBodyFacing::ResetDynamics(ETeleportType)
{
	PendingDeltaTime = Correction = InputYaw = Desired = 0.f;
	ReachScale = 1.f;
	LastUpdate.Reset();
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
	Input = {}; CachedNames.Reset(); Indices.Reset(); PelvisPath.Reset();
	ResetDynamics(ETeleportType::ResetPhysics);
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	CachedNames.Reset(); Indices.Reset(); PelvisPath.Reset();
	ResetDynamics(ETeleportType::ResetPhysics);
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::ResolveBones(const FBoneContainer& Bones)
{
	CachedNames = Roles(Input.Policy);
	Indices.Reset(); PelvisPath.Reset(); RootPosition = INDEX_NONE;
	for (const FName Name : CachedNames)
	{
		FBoneReference B; B.BoneName = Name; B.Initialize(Bones);
		if (!B.IsValidToEvaluate(Bones)) { Indices.Reset(); return; }
		const auto Index = B.GetCompactPoseIndex(Bones);
		if (Indices.Contains(Index)) { Indices.Reset(); return; }
		Indices.Add(Index);
	}
	for (int32 I = 2; I < Indices.Num(); ++I)
	{
		const int32 Parent = I == 2 || I == 3 || I == 6 ? 1 : I - 1;
		if (Bones.GetParentBoneIndex(Indices[I]) != Indices[Parent]) { Indices.Reset(); return; }
	}
	for (auto I = Indices[1]; I != INDEX_NONE && PelvisPath.Num() < 32; I = Bones.GetParentBoneIndex(I)) PelvisPath.Add(I);
	Algo::Reverse(PelvisPath);
	RootPosition = PelvisPath.Find(Indices[0]);
	if (RootPosition == INDEX_NONE || RootPosition >= PelvisPath.Num() - 1 || Bones.GetParentBoneIndex(PelvisPath[0]) != INDEX_NONE)
	{ Indices.Reset(); PelvisPath.Reset(); return; }
	FTransform Ref = FTransform::Identity;
	for (int32 I = 0; I < PelvisPath.Num(); ++I)
	{
		Ref = Bones.GetRefPoseTransform(PelvisPath[I]) * Ref;
		if (I == RootPosition) ReferenceRoot = Ref.GetRotation();
	}
	ReferencePelvis = Ref.GetRotation();
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
	const auto& Counter = Context.AnimInstanceProxy->GetUpdateCounter();
	if (!LastUpdate.WasSynchronizedCounter(Counter) || !LastUpdate.WasSynchronizedLastFrame(Counter)) Correction = PendingDeltaTime = 0.f;
	if (!LastUpdate.IsSynchronized_All(Counter)) { PendingDeltaTime += Context.GetDeltaTime(); LastUpdate.SynchronizeWith(Counter); }
	Input = {};
	if (const auto* Anim = Cast<UProject_JCharacterAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject()))
	{
		Input = Anim->GetThreadSafeStrafeLowerBodyFacing();
		const auto* Proxy = static_cast<const FProject_JCharacterAnimInstanceProxy*>(Context.AnimInstanceProxy);
		if (const auto* MM = Proxy->GetCapturedMotionMatchingNode())
			if (MM->GetMotionMatchingState().SearchResult.bIsInteraction) Input = {};
	}
	if (LODThreshold >= 0 && Context.AnimInstanceProxy->GetLODLevel() > LODThreshold) Input = {};
	if (!Input.Facing.bCanRelease) { Correction = PendingDeltaTime = 0.f; return; }
	if (CachedNames != Roles(Input.Policy)) { ResolveBones(Context.AnimInstanceProxy->GetRequiredBones()); Correction = 0.f; }
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
#if WITH_DEV_AUTOMATION_TESTS
	// Opt-in fixture evidence BEFORE any presentation change, including bypasses.
	// This attributes upstream Foot Placement startup variation correctly.
	static const bool bValidateSource = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeLowerBodyFacingTrial"));
	bValidationSourceFeetValid = false;
	if (bValidateSource)
	{
		if (CachedNames != Roles(Input.Policy)) ResolveBones(Output.Pose.GetBoneContainer());
		if (Indices.Num() == 9 && !PelvisPath.IsEmpty())
		{
			FTransform P = FTransform::Identity;
			for (auto I : PelvisPath) P = Output.Pose[I] * P;
			for (int32 L = 0; L < 2; ++L)
			{
				const int32 I = 3 + L * 3;
				ValidationSourceFeet[L] = Output.Pose[Indices[I+2]] * (Output.Pose[Indices[I+1]] * (Output.Pose[Indices[I]] * P));
			}
			bValidationSourceFeetValid = true;
		}
	}
#endif
	const float Dt = PendingDeltaTime; PendingDeltaTime = 0.f;
	InputYaw = Desired = 0.f; ReachScale = 1.f;
	if (!Input.Facing.bCanRelease || Indices.Num() != 9 || PelvisPath.IsEmpty()) { Correction = 0.f; return; }
	if (!Input.Facing.bEnabled && FMath::Abs(Correction) < .001f) { Correction = 0.f; return; }
	FTransform Pelvis = FTransform::Identity, Root;
	for (int32 I = 0; I < PelvisPath.Num(); ++I)
	{
		const auto& Local = Output.Pose[PelvisPath[I]];
		if (!Valid(Local)) { Correction = 0.f; return; }
		Pelvis = Local * Pelvis;
		if (I == RootPosition) Root = Pelvis;
	}
	for (int32 I = 2; I < Indices.Num(); ++I) if (!Valid(Output.Pose[Indices[I]])) { Correction = 0.f; return; }
	const FQuat Component = Output.AnimInstanceProxy->GetComponentTransform().GetRotation();
	if (Component.ContainsNaN() || !Component.IsNormalized()) { Correction = 0.f; return; }
	const FVector Neutral = Component.UnrotateVector(FRotator(0, Input.Facing.ActorYaw, 0).Vector());
	InputYaw = Heading(Pelvis.GetRotation(), ReferencePelvis, Component, Neutral);
	const float RootYaw = Heading(Root.GetRotation(), ReferenceRoot, Component, Neutral);
	if (!FMath::IsFinite(InputYaw) || !FMath::IsFinite(RootYaw)) { Correction = 0.f; return; }
	const float Strength = Input.Facing.Strength * Project_J::Animation::UpperBodyRootFacingWeight(
		FMath::FindDeltaAngleDegrees(RootYaw, Input.Facing.TargetYaw), Input.Facing.Policy);
	Desired = Input.Facing.bEnabled ? Project_J::Animation::UpperBodyFacingDemand(
		FMath::FindDeltaAngleDegrees(InputYaw, Input.Facing.TargetYaw), Strength, Input.Facing.Policy) : 0.f;
	float Requested = Project_J::Animation::StepUpperBodyFacing(Correction, Desired, Dt, Input.Facing.Policy);
	if (FMath::Abs(Requested) < .001f) { Correction = 0.f; return; }
	const FTransform Spine = Output.Pose[Indices[2]] * Pelvis;
	FTransform FootTargets[2], Solved[2][3], Warped;
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		const int32 I = 3 + Leg * 3;
		FootTargets[Leg] = Output.Pose[Indices[I+2]] * (Output.Pose[Indices[I+1]] * (Output.Pose[Indices[I]] * Pelvis));
	}
	const FVector Axis = Component.UnrotateVector(FVector::UpVector).GetSafeNormal();
	auto Solve = [&](float Yaw)
	{
		Warped = Pelvis;
		Warped.SetRotation((FQuat(Axis, FMath::DegreesToRadians(Yaw)) * Pelvis.GetRotation()).GetNormalized());
		for (int32 Leg = 0; Leg < 2; ++Leg)
		{
			const int32 I = 3 + Leg * 3;
			if (!Project_J::Animation::SolveFacingLeg(Warped, Output.Pose[Indices[I]], Output.Pose[Indices[I+1]],
				Output.Pose[Indices[I+2]], FootTargets[Leg], Solved[Leg][0], Solved[Leg][1], Solved[Leg][2])) return false;
		}
		return true;
	};
	// Feet win over desired facing. Reduce yaw rather than stretching a straight leg.
	if (!Solve(Requested))
	{
		float Low = 0.f, High = 1.f;
		if (!Solve(0.f)) { Correction = 0.f; ReachScale = 0.f; return; }
		for (int32 I = 0; I < 8; ++I)
		{
			const float Mid = (Low + High) * .5f;
			if (Solve(Requested * Mid)) Low = Mid; else High = Mid;
		}
		ReachScale = Low;
		Requested *= Low;
		if (FMath::Abs(Requested) < .001f || !Solve(Requested)) { Correction = 0.f; return; }
	}
	const auto& Bones = Output.Pose.GetBoneContainer();
	const auto ParentIndex = Bones.GetParentBoneIndex(Indices[1]);
	// Compose only the pelvis ancestors; root and every attribute remain untouched.
	FTransform Parent = FTransform::Identity;
	for (int32 I = 0; I + 1 < PelvisPath.Num(); ++I) Parent = Output.Pose[PelvisPath[I]] * Parent;
	if (ParentIndex == INDEX_NONE) { Correction = 0.f; return; }
	Output.Pose[Indices[1]].SetRotation((Parent.GetRotation().Inverse() * Warped.GetRotation()).GetNormalized());
	Output.Pose[Indices[2]].SetRotation((Warped.GetRotation().Inverse() * Spine.GetRotation()).GetNormalized());
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		const int32 I = 3 + Leg * 3;
		Output.Pose[Indices[I]] = Solved[Leg][0].GetRelativeTransform(Warped);
		Output.Pose[Indices[I+1]] = Solved[Leg][1].GetRelativeTransform(Solved[Leg][0]);
		Output.Pose[Indices[I+2]] = Solved[Leg][2].GetRelativeTransform(Solved[Leg][1]);
	}
	Correction = Requested;
}
void FAnimNode_ProjectJStrafeLowerBodyFacing::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(FString::Printf(TEXT("%s Enabled=%d Pelvis=%.2f Desired=%.2f Applied=%.2f Reach=%.3f"),
		*DebugData.GetNodeName(this), WasEnabled(), InputYaw, Desired, Correction, ReachScale));
	Source.GatherDebugData(DebugData);
}
