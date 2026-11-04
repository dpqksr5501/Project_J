#include "Validation/Project_JMeleeTrajectoryAssetValidator.h"
#include "Authoring/Project_JCanonicalMeleeAuthoring.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/DataValidation.h"
#include "UObject/StrongObjectPtr.h"

bool UProject_JMeleeTrajectoryAssetValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
    const auto* Attack=Cast<UProject_JAttackDefinition>(Object);
    return Attack && Attack->MeleeTrajectory.bEnabled;
}

EDataValidationResult UProject_JMeleeTrajectoryAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
    TStrongObjectPtr<UProject_JAttackDefinition> Attack(CastChecked<UProject_JAttackDefinition>(Object));
    // Selected soft dependency only. No asset scan, pose bake or package write.
    TStrongObjectPtr<USkeletalMesh> Mesh(Attack->MeleeTrajectory.SourceMesh.LoadSynchronous());
    FString Failure;
    if (!Mesh.IsValid() || !Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*Attack, *Mesh, Attack->MeleeTrajectory, Failure))
    {
        if (Failure.IsEmpty()) Failure=TEXT("Selected melee source mesh cannot be loaded.");
        AssetFails(Object,FText::FromString(Failure));
        return EDataValidationResult::Invalid;
    }
    AssetPasses(Object);
    return EDataValidationResult::Valid;
}
