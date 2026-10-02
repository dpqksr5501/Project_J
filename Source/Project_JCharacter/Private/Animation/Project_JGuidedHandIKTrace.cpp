#include "Animation/Project_JGuidedHandIKTrace.h"

#if !UE_BUILD_SHIPPING
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJGuidedIK, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarTrace(TEXT("ProjectJ.Animation.GuidedIKTrace"), 0,
	TEXT("Log guided hand IK input, full solve and a copy of its exact engine alpha blend. Development only."));
TAutoConsoleVariable<float> CVarTraceHz(TEXT("ProjectJ.Animation.GuidedIKTraceHz"), 60.0f,
	TEXT("Maximum guided IK samples per second per node (1-120). No extra animation evaluations."));
TAutoConsoleVariable<FString> CVarTraceActor(TEXT("ProjectJ.Animation.GuidedIKTraceActor"), FString(),
	TEXT("Optional case-insensitive owning actor name substring filter."));

struct FTraceArmPose
{
	FVector Shoulder = FVector::ZeroVector, Elbow = FVector::ZeroVector;
	FVector Wrist = FVector::ZeroVector, Plane = FVector::ZeroVector;
	double Angle = -1.0, BendHeight = 0.0;
};

FTraceArmPose Describe(const FTransform* Bones)
{
	FTraceArmPose Result;
	Result.Shoulder = Bones[0].GetLocation();
	Result.Elbow = Bones[1].GetLocation();
	Result.Wrist = Bones[2].GetLocation();
	const FVector Upper = Result.Elbow - Result.Shoulder;
	const FVector Lower = Result.Wrist - Result.Elbow;
	if (!Upper.IsNearlyZero() && !Lower.IsNearlyZero())
	{
		Result.Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(-Upper.GetSafeNormal(), Lower.GetSafeNormal()), -1.0, 1.0)));
	}
	const FVector Aim = (Result.Wrist - Result.Shoulder).GetSafeNormal();
	Result.BendHeight = (Upper - Aim * FVector::DotProduct(Upper, Aim)).Size();
	// Diagnostic planes near a straight arm are ill-conditioned. This threshold
	// does NOT change the solver's existing fallback threshold or behaviour.
	if (Result.BendHeight > 0.5) { Result.Plane = FVector::CrossProduct(Upper, Lower).GetSafeNormal(); }
	return Result;
}
}

struct FProject_JGuidedHandIKTraceState
{
	const void* NodeIdentity = nullptr;
	FString Actor, Mesh, World, Hand, Forearm, UpperArm;
	int32 NetMode = 0;
	uint64 Epoch = 0, PreFrame = 0, LoggedEpoch = 0;
	double Time = 0.0, LastWorldTime = -1.0, NextSample = 0.0, PreviousTime = 0.0;
	bool bCapture = false, bHasPrevious = false;
	float PreviousAlpha = 0.0f;
	FVector PreviousTarget = FVector::ZeroVector;
	FTraceArmPose Previous[3];
};

void SnapshotGuidedIKTrace(TSharedPtr<FProject_JGuidedHandIKTraceState>& State,
	const void* NodeIdentity, const UAnimInstance* Instance, FName HandBone, FName ForearmBone, FName UpperArmBone)
{
	if (CVarTrace.GetValueOnGameThread() == 0)
	{
		if (State) { State->bCapture = false; State->bHasPrevious = false; State->NextSample = 0.0; }
		return;
	}
	const USkeletalMeshComponent* Mesh = Instance ? Instance->GetSkelMeshComponent() : nullptr;
	const AActor* Actor = Mesh ? Mesh->GetOwner() : nullptr;
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	const FString Filter = CVarTraceActor.GetValueOnGameThread();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer ||
		(!Filter.IsEmpty() && !Actor->GetName().Contains(Filter, ESearchCase::IgnoreCase)))
	{
		if (State) { State->bCapture = false; State->bHasPrevious = false; State->NextSample = 0.0; }
		return;
	}
	if (!State || State->NodeIdentity != NodeIdentity) { State = MakeShared<FProject_JGuidedHandIKTraceState>(); }
	State->NodeIdentity = NodeIdentity;
	State->Time = World->GetTimeSeconds();
	if (State->Time < State->LastWorldTime)
	{
		State->NextSample = 0.0;
		State->bHasPrevious = false;
	}
	State->LastWorldTime = State->Time;
	State->PreFrame = GFrameCounter; // PreUpdate epoch, not proof of the consumed source pose's frame.
	++State->Epoch;
	State->bCapture = State->Time >= State->NextSample;
	if (!State->bCapture) { return; }
	State->NextSample = State->Time + 1.0 / FMath::Clamp(CVarTraceHz.GetValueOnGameThread(), 1.0f, 120.0f);
	// All UObject access and actor filtering happen on the game thread.
	State->Actor = Actor->GetName();
	State->Mesh = Mesh->GetName();
	State->World = World->GetName();
	State->NetMode = static_cast<int32>(World->GetNetMode());
	State->Hand = HandBone.ToString();
	const FName Forearm = ForearmBone.IsNone() ? Mesh->GetParentBone(HandBone) : ForearmBone;
	State->Forearm = Forearm.ToString();
	State->UpperArm = (UpperArmBone.IsNone() ? Mesh->GetParentBone(Forearm) : UpperArmBone).ToString();
}

bool ShouldCaptureGuidedIKTrace(const TSharedPtr<FProject_JGuidedHandIKTraceState>& State)
{
	return State && State->bCapture && State->LoggedEpoch != State->Epoch;
}

void LogGuidedIKTrace(FProject_JGuidedHandIKTraceState& State, FComponentSpacePoseContext& Output,
	const FTransform* Input, const FTransform* Solved, const FTransform& Target,
	float InputAlpha, float ActualAlpha, const TCHAR* Status,
	const TArray<FBoneTransform>* BoneTransforms,
	const Project_J::Animation::FGuidedArmSolveDiagnostics* Diagnostics, const FCompactPoseBoneIndex* ArmIndices)
{
	State.LoggedEpoch = State.Epoch;
	const double Dt = State.bHasPrevious ? State.Time - State.PreviousTime : 0.0;
	const bool bHistoryValid = State.bHasPrevious && Dt > UE_KINDA_SMALL_NUMBER && Dt <= 0.25;
	UE_LOG(LogProjectJGuidedIK, Display,
		TEXT("[GuidedIK][State] t=%.4f preFrame=%llu epoch=%llu node=%p actor=%s mesh=%s world=%s net=%d hand=%s elbow=%s shoulder=%s status=%s alphaIn=%.4f alpha=%.4f alphaDelta=%.4f lod=%d hist=%d dt=%.4f targetStep=%.3f targetCS=%s targetRotCS=%s fallback=%d bestAxis=%d antipodal=%d clampMin=%d clampMax=%d upper=%.3f lower=%.3f reach=%.3f inputBend=%.5f guideCS=%s reachableCS=%s rawGuideCS=%s guideHistory=%d guideReacquire=%d guideCorrection=%.3f"),
		State.Time, static_cast<unsigned long long>(State.PreFrame), static_cast<unsigned long long>(State.Epoch),
		State.NodeIdentity, *State.Actor, *State.Mesh, *State.World, State.NetMode, *State.Hand, *State.Forearm, *State.UpperArm,
		Status, InputAlpha, ActualAlpha, bHistoryValid ? ActualAlpha - State.PreviousAlpha : 0.0f,
		Output.AnimInstanceProxy->GetLODLevel(), bHistoryValid, Dt,
		bHistoryValid ? FVector::Distance(Target.GetLocation(), State.PreviousTarget) : -1.0,
		*Target.GetLocation().ToCompactString(), *Target.Rotator().ToCompactString(),
		Diagnostics ? Diagnostics->bUsedFallback : -1, Diagnostics ? Diagnostics->bUsedBestAxis : -1,
		Diagnostics ? Diagnostics->bAntipodal : -1, Diagnostics ? Diagnostics->bClampedMinimum : -1,
		Diagnostics ? Diagnostics->bClampedMaximum : -1, Diagnostics ? Diagnostics->UpperLength : -1.0,
		Diagnostics ? Diagnostics->LowerLength : -1.0, Diagnostics ? Diagnostics->TargetDistance : -1.0,
		Diagnostics ? Diagnostics->InputBendHeight : -1.0,
		*(Diagnostics ? Diagnostics->BendDirection : FVector::ZeroVector).ToCompactString(),
		*(Diagnostics ? Diagnostics->ReachableTarget : FVector::ZeroVector).ToCompactString(),
		*(Diagnostics ? Diagnostics->RawBendDirection : FVector::ZeroVector).ToCompactString(),
		Diagnostics ? Diagnostics->bGuideHistory : -1, Diagnostics ? Diagnostics->bGuideReacquiring : -1,
		Diagnostics ? Diagnostics->GuideCorrectionDegrees : -1.0);
	if (!Input || !Solved) { State.bHasPrevious = false; return; }

	FTransform Blended[3] = { Input[0], Input[1], Input[2] };
	if (BoneTransforms && BoneTransforms->Num() >= 3 && (ArmIndices || BoneTransforms->Num() == 3))
	{
		// SkeletalControlBase's final Evaluate cannot be overridden. Probe its
		// exact alpha operation on an isolated pose copy, never the live output.
		FComponentSpacePoseContext Probe(Output);
		Probe.Pose.CopyPose(Output.Pose);
		Probe.Pose.LocalBlendCSBoneTransforms(*BoneTransforms, FMath::Clamp(ActualAlpha, 0.0f, 1.0f));
		for (int32 I = 0; I < 3; ++I)
		{
			Blended[I] = Probe.Pose.GetComponentSpaceTransform(ArmIndices ? ArmIndices[I] : (*BoneTransforms)[I].BoneIndex);
		}
	}
	const FTransform* Stages[] = { Input, Solved, Blended };
	const TCHAR* Names[] = { TEXT("Input"), TEXT("Solve"), TEXT("PostAlphaProbe") };
	for (int32 I = 0; I < 3; ++I)
	{
		const FTraceArmPose Pose = Describe(Stages[I]);
		const bool bPlaneValid = !Pose.Plane.IsNearlyZero();
		const bool bPlaneHistory = bHistoryValid && bPlaneValid && !State.Previous[I].Plane.IsNearlyZero();
		UE_LOG(LogProjectJGuidedIK, Display,
			TEXT("[GuidedIK][Pose] preFrame=%llu epoch=%llu node=%p actor=%s mesh=%s world=%s stage=%s shoulderCS=%s elbowCS=%s wristCS=%s wristRotCS=%s angle=%.3f bendHeight=%.4f planeValid=%d planeCS=%s planeHistory=%d planeDot=%.4f elbowStep=%.3f wristStep=%.3f contactErr=%.3f rotationErr=%.3f"),
			static_cast<unsigned long long>(State.PreFrame), static_cast<unsigned long long>(State.Epoch), State.NodeIdentity,
			*State.Actor, *State.Mesh, *State.World, Names[I], *Pose.Shoulder.ToCompactString(), *Pose.Elbow.ToCompactString(),
			*Pose.Wrist.ToCompactString(), *Stages[I][2].Rotator().ToCompactString(), Pose.Angle, Pose.BendHeight,
			bPlaneValid, *Pose.Plane.ToCompactString(), bPlaneHistory,
			bPlaneHistory ? FVector::DotProduct(Pose.Plane, State.Previous[I].Plane) : 1.0,
			bHistoryValid ? FVector::Distance(Pose.Elbow, State.Previous[I].Elbow) : -1.0,
			bHistoryValid ? FVector::Distance(Pose.Wrist, State.Previous[I].Wrist) : -1.0,
			FVector::Distance(Pose.Wrist, Target.GetLocation()),
			FMath::RadiansToDegrees(Stages[I][2].GetRotation().AngularDistance(Target.GetRotation())));
		State.Previous[I] = Pose;
	}
	UE_LOG(LogProjectJGuidedIK, Display,
		TEXT("[GuidedIK][Correction] preFrame=%llu epoch=%llu node=%p actor=%s mesh=%s world=%s solveElbowOffset=%.3f blendedElbowOffset=%.3f solvedWristOffset=%.3f blendedWristOffset=%.3f"),
		static_cast<unsigned long long>(State.PreFrame), static_cast<unsigned long long>(State.Epoch), State.NodeIdentity,
		*State.Actor, *State.Mesh, *State.World, FVector::Distance(Input[1].GetLocation(), Solved[1].GetLocation()),
		FVector::Distance(Input[1].GetLocation(), Blended[1].GetLocation()),
		FVector::Distance(Input[2].GetLocation(), Solved[2].GetLocation()),
		FVector::Distance(Input[2].GetLocation(), Blended[2].GetLocation()));
	State.PreviousTime = State.Time;
	State.PreviousTarget = Target.GetLocation();
	State.PreviousAlpha = ActualAlpha;
	State.bHasPrevious = true;
}
#endif
