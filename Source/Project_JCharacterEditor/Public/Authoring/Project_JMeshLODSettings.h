#pragma once
#include "Engine/SkeletalMeshLODSettings.h"
#include "StaticMeshEditorSubsystemHelpers.h"
#include "Project_JMeshLODSettings.generated.h"

/** Shared editor recipe using the engine's native reduction settings. Never applied to LOD0 or to an existing LOD chain. */
UCLASS(BlueprintType)
class PROJECT_JCHARACTEREDITOR_API UProject_JMeshLODSettings : public UDataAsset
{
    GENERATED_BODY()
public:
    UProject_JMeshLODSettings(const FObjectInitializer& Initializer);
    UPROPERTY(VisibleAnywhere,Instanced,Category="Skeletal",meta=(ShowOnlyInnerProperties)) TObjectPtr<USkeletalMeshLODSettings> SkeletalLODs;
    UPROPERTY(EditAnywhere, Category="Equipment") FStaticMeshReductionOptions StaticMeshLODs;
    /** Paths for the engine's read-only Data Validation integration, not a recursive generation list. */
    UPROPERTY(EditAnywhere, Category="Validation") TArray<FString> ValidationRoots;
    /** Explicit native authoring workflow for card hair/fur or deformation-sensitive meshes. Preserves generic-reduction exclusion; validation reports information instead of a recurring review warning. Invalid LOD0 and platform MinLOD checks still apply. */
    UPROPERTY(EditAnywhere, Category="Validation") TArray<FSoftObjectPath> ManualReviewAssets;
    virtual bool IsEditorOnly() const override { return true; }
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
};
