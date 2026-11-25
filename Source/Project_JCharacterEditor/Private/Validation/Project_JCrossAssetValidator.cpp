#include "Validation/Project_JCrossAssetValidator.h"

namespace Project_J::EditorValidation
{
FCrossAssetRuleResult ValidateNPCAnimPolicy(FName AssetName, bool bUsesPoseSearchOrChooser)
{
	FCrossAssetRuleResult Res;
	if (bUsesPoseSearchOrChooser)
	{
		Res.bPassed = false;
		Res.Errors.Add(FString::Printf(
			TEXT("NPC asset '%s' violates NPC non-Motion-Matching policy (PoseSearch/Chooser forbidden on NPCs)."),
			*AssetName.ToString()));
	}
	return Res;
}

FCrossAssetRuleResult ValidateFoliageAssetPolicy(FName AssetName, FName AssignedHLODLayer, float WPODisableDistanceCm, float MaxAllowedWPOCm)
{
	FCrossAssetRuleResult Res;
	if (AssignedHLODLayer != FName(TEXT("HLODLayer_Foliage_Impostor")))
	{
		Res.bPassed = false;
		Res.Errors.Add(FString::Printf(
			TEXT("Foliage asset '%s' must use HLODLayer_Foliage_Impostor (found '%s') to prevent Embree OOM."),
			*AssetName.ToString(), *AssignedHLODLayer.ToString()));
	}
	if (WPODisableDistanceCm <= 0.0f || WPODisableDistanceCm > MaxAllowedWPOCm)
	{
		Res.bPassed = false;
		Res.Errors.Add(FString::Printf(
			TEXT("Foliage asset '%s' has invalid WorldPositionOffsetDisableDistance=%.1f (must be in (0, %.1f] for VSM static cache)."),
			*AssetName.ToString(), WPODisableDistanceCm, MaxAllowedWPOCm));
	}
	return Res;
}

FCrossAssetRuleResult ValidateScalabilitySkeletalRTPolicy(const FString& ScalabilityIniContent)
{
	FCrossAssetRuleResult Res;
	TArray<FString> Lines;
	ScalabilityIniContent.ParseIntoArrayLines(Lines);

	FString CurrentSection;
	for (const FString& Raw : Lines)
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")))
		{
			continue;
		}
		if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
		{
			CurrentSection = Line;
			continue;
		}
		const bool bRuntimeTier = CurrentSection.Contains(TEXT("@2")) || CurrentSection.Contains(TEXT("@3"));
		if (bRuntimeTier && Line.Contains(TEXT("r.RayTracing.Geometry.SkeletalMeshes=1")))
		{
			Res.bPassed = false;
			Res.Errors.Add(FString::Printf(TEXT("Runtime scalability section %s must keep r.RayTracing.Geometry.SkeletalMeshes=0 for MMO crowd frames."), *CurrentSection));
		}
		if (bRuntimeTier && Line.Contains(TEXT("r.MeshCardRepresentation.SkeletalMesh=1")))
		{
			Res.bPassed = false;
			Res.Errors.Add(FString::Printf(TEXT("Runtime scalability section %s must keep r.MeshCardRepresentation.SkeletalMesh=0 for MMO crowd frames."), *CurrentSection));
		}
	}
	return Res;
}
}
