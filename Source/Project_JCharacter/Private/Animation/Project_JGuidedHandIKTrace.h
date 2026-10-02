#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/Project_JGuidedArmSolver.h"

#if !UE_BUILD_SHIPPING
struct FProject_JGuidedHandIKTraceState;

void SnapshotGuidedIKTrace(TSharedPtr<FProject_JGuidedHandIKTraceState>& State,
	const void* NodeIdentity, const UAnimInstance* Instance, FName HandBone,
	FName ForearmBone = NAME_None, FName UpperArmBone = NAME_None);
bool ShouldCaptureGuidedIKTrace(const TSharedPtr<FProject_JGuidedHandIKTraceState>& State);
void LogGuidedIKTrace(FProject_JGuidedHandIKTraceState& State, FComponentSpacePoseContext& Output,
	const FTransform* Input, const FTransform* Solved, const FTransform& Target,
	float InputAlpha, float ActualAlpha, const TCHAR* Status,
	const TArray<FBoneTransform>* BoneTransforms = nullptr,
	const Project_J::Animation::FGuidedArmSolveDiagnostics* SolveDiagnostics = nullptr,
	const FCompactPoseBoneIndex* ArmIndices = nullptr);
#endif
