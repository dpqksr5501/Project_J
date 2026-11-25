#pragma once

#include "CoreMinimal.h"

namespace Project_J::EditorValidation
{
struct FCrossAssetRuleResult
{
	bool bPassed = true;
	TArray<FString> Errors;
};

/**
 * Pure authoring rule validators run by editor/CI guardrails (BP06 / Stage A):
 *  1. Enforces NPC non-Motion-Matching rule (blocks PoseSearch/Chooser graphs on NPCs).
 *  2. Checks starter forest instancing HLOD and the distance cutoff for active WPO.
 *  3. Enforces Skeletal Ray Tracing OFF on High/Epic scalability tiers (@2/@3).
 */
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateNPCAnimPolicy(FName AssetName, bool bUsesPoseSearchOrChooser);
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateFoliageAssetPolicy(
	const FString& ComponentPath, bool bUsesInstancingHLOD, TOptional<int32> WPODisableDistanceCm);
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateScalabilitySkeletalRTPolicy(const FString& ScalabilityIniContent);
}
