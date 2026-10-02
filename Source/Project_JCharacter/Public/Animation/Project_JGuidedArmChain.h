#pragma once
#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BonePose.h"

namespace Project_J::Animation
{
	struct FGuidedArmChain
	{
		FCompactPoseBoneIndex Upper = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Forearm = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Hand = FCompactPoseBoneIndex(INDEX_NONE);
		TArray<FCompactPoseBoneIndex> Path;
		int32 ForearmPathIndex = INDEX_NONE;
		bool IsValid() const { return Path.Num() >= 3 && ForearmPathIndex > 0 && ForearmPathIndex < Path.Num() - 1; }
	};

	/** Explicit roles may span helper bones. Empty upper/forearm names retain the legacy immediate-parent chain. */
	PROJECT_JCHARACTER_API bool ResolveGuidedArmChain(const FBoneContainer& Bones, FName Hand,
		FName Forearm, FName Upper, FGuidedArmChain& OutChain);

	/** Preserve each intervening helper's input transform relative to its anatomical segment root. */
	PROJECT_JCHARACTER_API bool AppendGuidedArmTransforms(const FGuidedArmChain& Chain,
		FCSPose<FCompactPose>& Pose, const FTransform* InputArm, const FTransform* SolvedArm,
		TArray<FBoneTransform>& OutTransforms);
}
