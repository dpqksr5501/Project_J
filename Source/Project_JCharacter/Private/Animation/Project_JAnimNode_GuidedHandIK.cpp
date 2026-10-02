#include "Animation/Project_JAnimNode_GuidedHandIK.h"
#include "Animation/Project_JGuidedArmSolver.h"
#include "Animation/Project_JGuidedHandIKTrace.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JHandContact.h"
#include "Components/SkeletalMeshComponent.h"

void FProject_JAnimNode_GuidedHandIK::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	bProfileArmValid = false;
	Chain = {};
	CachedArmNames = {};
	ResetDynamics(ETeleportType::ResetPhysics);
}

void FProject_JAnimNode_GuidedHandIK::ResetDynamics(ETeleportType)
{
	BendState.Reset();
	LastActiveUpdate.Reset();
	PendingGuideDeltaTime = 0.0;
}

void FProject_JAnimNode_GuidedHandIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	const FGraphTraversalCounter& Counter = Context.AnimInstanceProxy->GetUpdateCounter();
	if (!LastActiveUpdate.WasSynchronizedCounter(Counter) || !LastActiveUpdate.WasSynchronizedLastFrame(Counter))
	{
		BendState.Reset();
		PendingGuideDeltaTime = 0.0;
	}
	if (!LastActiveUpdate.IsSynchronized_All(Counter))
	{
		PendingGuideDeltaTime += Context.GetDeltaTime();
		LastActiveUpdate.SynchronizeWith(Counter);
	}
}

void FProject_JAnimNode_GuidedHandIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	bProfileArmValid = false;
	if (ArmDefinitionSource != EProject_JGuidedArmDefinitionSource::NodeSettings)
	{
		const UProject_JRetargetAnimInstance* Retarget = Cast<UProject_JRetargetAnimInstance>(InAnimInstance);
		const USkeletalMeshComponent* Mesh = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
		if (Retarget && Mesh)
		{
			const FProject_JHandGripCalibration Calibration = Retarget->GetHandGripCalibration();
			const bool bPrimary = ArmDefinitionSource == EProject_JGuidedArmDefinitionSource::PrimaryBodyProfile;
			ProfileArmSnapshot = bPrimary ? Calibration.PrimaryArm : Calibration.SecondaryArm;
			ProfileStabilitySnapshot = bPrimary ? Calibration.PrimaryBendStability : Calibration.SecondaryBendStability;
			const Project_J::Animation::FResolvedHandContact Contact = Project_J::Animation::ResolveHandContact(*Mesh,
				bPrimary ? Calibration.PrimaryPalmSocketName : Calibration.SecondaryPalmSocketName,
				ProfileArmSnapshot.Hand, Calibration.MissingPalmPolicy == EProject_JMissingPalmPolicy::LegacyWristOrigin);
			ProfileArmSnapshot.Hand = Contact.Hand;
			bProfileArmValid = Contact.IsValid() && !Contact.Hand.IsNone() && ProfileStabilitySnapshot.IsValid();
		}
	}
#if !UE_BUILD_SHIPPING
	const FProject_JGripArmBones Requested = GetRequestedArm();
	SnapshotGuidedIKTrace(TraceState, this, InAnimInstance, Requested.Hand, Requested.Elbow, Requested.Shoulder);
#endif
}

FProject_JGripArmBones FProject_JAnimNode_GuidedHandIK::GetRequestedArm() const
{
	if (ArmDefinitionSource != EProject_JGuidedArmDefinitionSource::NodeSettings)
	{
		return bProfileArmValid ? ProfileArmSnapshot : FProject_JGripArmBones();
	}
	FProject_JGripArmBones Names;
	Names.Hand = HandBone.BoneName;
	Names.Elbow = ForearmBone.BoneName;
	Names.Shoulder = UpperArmBone.BoneName;
	return Names;
}

void FProject_JAnimNode_GuidedHandIK::EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output)
{
	FAnimNode_SkeletalControlBase::EvaluateComponentPose_AnyThread(Output);
	if (!FAnimWeight::IsRelevant(ActualAlpha) || !IsLODEnabled(Output.AnimInstanceProxy) ||
		!IsValidToEvaluate(nullptr, Output.Pose.GetPose().GetBoneContainer()))
	{
		ResetDynamics(ETeleportType::ResetPhysics);
	}
#if !UE_BUILD_SHIPPING
	if (!ShouldCaptureGuidedIKTrace(TraceState)) { return; }
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const bool bChainValid = IsValidToEvaluate(nullptr, Bones);
	if (!bChainValid)
	{
		LogGuidedIKTrace(*TraceState, Output, nullptr, nullptr, EffectorTransform, Alpha, ActualAlpha, TEXT("InvalidChain"));
	}
	else if (!FAnimWeight::IsRelevant(ActualAlpha))
	{
		const FTransform Input[] = {
			Output.Pose.GetComponentSpaceTransform(Chain.Upper),
			Output.Pose.GetComponentSpaceTransform(Chain.Forearm),
			Output.Pose.GetComponentSpaceTransform(Chain.Hand) };
		LogGuidedIKTrace(*TraceState, Output, Input, Input, EffectorTransform, Alpha, ActualAlpha,
			IsLODEnabled(Output.AnimInstanceProxy) ? TEXT("AlphaBypass") : TEXT("LODBypass"));
	}
#endif
}

void FProject_JAnimNode_GuidedHandIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	ResetDynamics(ETeleportType::ResetPhysics);
	CachedArmNames = GetRequestedArm();
	CachedArmSource = ArmDefinitionSource;
	Project_J::Animation::ResolveGuidedArmChain(RequiredBones, CachedArmNames.Hand,
		CachedArmNames.Elbow, CachedArmNames.Shoulder, Chain);
}

bool FProject_JAnimNode_GuidedHandIK::IsValidToEvaluate(const USkeleton*, const FBoneContainer& RequiredBones)
{
	const FProject_JGripArmBones Requested = GetRequestedArm();
	if (Requested.Hand != CachedArmNames.Hand || Requested.Elbow != CachedArmNames.Elbow ||
		Requested.Shoulder != CachedArmNames.Shoulder || CachedArmSource != ArmDefinitionSource)
	{
		InitializeBoneReferences(RequiredBones);
	}
	return Chain.IsValid();
}

void FProject_JAnimNode_GuidedHandIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	const FTransform& UpperArm = Output.Pose.GetComponentSpaceTransform(Chain.Upper);
	const FTransform& Forearm = Output.Pose.GetComponentSpaceTransform(Chain.Forearm);
	const FTransform& Hand = Output.Pose.GetComponentSpaceTransform(Chain.Hand);
	FTransform SolvedUpperArm, SolvedForearm, SolvedHand;
	Project_J::Animation::FGuidedArmBendSettings Settings;
	Settings.HoldBendRatio = HoldBendRatio;
	Settings.ReliableBendRatio = ReliableBendRatio;
	Settings.ReacquireDegreesPerSecond = ReacquireDegreesPerSecond;
	Settings.MaxHistorySeconds = MaxGuideHistorySeconds;
	bool bStabilize = bStabilizeBendPlane;
	FVector PoleAxis = FallbackPoleAxis;
	if (ArmDefinitionSource != EProject_JGuidedArmDefinitionSource::NodeSettings)
	{
		Settings.HoldBendRatio = ProfileStabilitySnapshot.HoldBendRatio;
		Settings.ReliableBendRatio = ProfileStabilitySnapshot.ReliableBendRatio;
		Settings.ReacquireDegreesPerSecond = ProfileStabilitySnapshot.ReacquireDegreesPerSecond;
		Settings.MaxHistorySeconds = ProfileStabilitySnapshot.MaxHistorySeconds;
		bStabilize = ProfileStabilitySnapshot.bEnabled;
		PoleAxis = ProfileStabilitySnapshot.FallbackPoleAxis;
	}
	if (!bStabilize) { BendState.Reset(); }
	Project_J::Animation::FGuidedArmSolveDiagnostics* Diagnostics = nullptr;
#if !UE_BUILD_SHIPPING
	Project_J::Animation::FGuidedArmSolveDiagnostics TraceDiagnostics;
	if (ShouldCaptureGuidedIKTrace(TraceState)) { Diagnostics = &TraceDiagnostics; }
#endif
	const bool bSolved = Project_J::Animation::SolveGuidedArm(UpperArm, Forearm, Hand, EffectorTransform,
		UpperArm.GetRotation().RotateVector(PoleAxis), bUseExplicitElbowGuide ? &ElbowGuideLocation : nullptr,
		bMatchWristRotation, SolvedUpperArm, SolvedForearm, SolvedHand, Diagnostics,
		bStabilize ? &BendState : nullptr, &Settings, PendingGuideDeltaTime);
	// Updates skipped by URO accumulate elapsed time once; repeated evaluations
	// without a new update must not advance the guide a second time.
	PendingGuideDeltaTime = 0.0;
#if !UE_BUILD_SHIPPING
	if (!bSolved && ShouldCaptureGuidedIKTrace(TraceState))
	{
		const FTransform Input[] = { UpperArm, Forearm, Hand };
		LogGuidedIKTrace(*TraceState, Output, Input, Input, EffectorTransform, Alpha, ActualAlpha, TEXT("SolveFailed"));
	}
#endif
	if (!bSolved) { BendState.Reset(); return; }

	// SkeletalControlBase applies the graph's Alpha exactly once and bypasses
	// the solve at zero. The guide is reset on bypass, reinit, gaps and teleports.
	const FTransform Input[] = { UpperArm, Forearm, Hand };
	const FTransform Solved[] = { SolvedUpperArm, SolvedForearm, SolvedHand };
	if (!Project_J::Animation::AppendGuidedArmTransforms(Chain, Output.Pose, Input, Solved, OutBoneTransforms))
	{
		BendState.Reset();
		return;
	}
#if !UE_BUILD_SHIPPING
	if (ShouldCaptureGuidedIKTrace(TraceState))
	{
		const FCompactPoseBoneIndex ArmIndices[] = { Chain.Upper, Chain.Forearm, Chain.Hand };
		LogGuidedIKTrace(*TraceState, Output, Input, Solved, EffectorTransform, Alpha, ActualAlpha,
			TEXT("Solved"), &OutBoneTransforms, Diagnostics, ArmIndices);
	}
#endif
}
