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
 *  2. Enforces 8x8km Foliage HLODLayer_Foliage_Impostor + WPO distance cut (prevents 91GB Embree OOM & VSM thrash).
 *  3. Enforces Skeletal Ray Tracing OFF on High/Epic scalability tiers (@2/@3).
 */
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateNPCAnimPolicy(FName AssetName, bool bUsesPoseSearchOrChooser);
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateFoliageAssetPolicy(FName AssetName, FName AssignedHLODLayer, float WPODisableDistanceCm, float MaxAllowedWPOCm = 10000.0f);
PROJECT_JCHARACTEREDITOR_API FCrossAssetRuleResult ValidateScalabilitySkeletalRTPolicy(const FString& ScalabilityIniContent);
}
