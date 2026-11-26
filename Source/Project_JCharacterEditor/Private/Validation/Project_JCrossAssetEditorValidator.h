#pragma once

#include "EditorValidatorBase.h"
#include "Project_JCrossAssetEditorValidator.generated.h"

/**
 * Automatic UE Editor Save & DataValidation guardrail (BP06 / Stage A):
 *  - Blocks PoseSearch/Chooser Motion Matching nodes on NPC AnimBlueprints (NPC non-MM invariant).
 *  - Validates UProject_JNPCBrainDefinition leash/sight bounds.
 *  - Validates distant-mesh intent in loaded WP placements and associated external save objects.
 */
UCLASS()
class UProject_JCrossAssetEditorValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) override;
};
