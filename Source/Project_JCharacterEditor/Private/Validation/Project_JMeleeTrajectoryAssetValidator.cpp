#include "Validation/Project_JMeleeTrajectoryAssetValidator.h"
#include "Authoring/Project_JCanonicalMeleeAuthoring.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
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
    if (!Mesh.IsValid())
    {
        AssetFails(Object,FText::FromString(TEXT("Selected melee source mesh cannot be loaded.")));
        return EDataValidationResult::Invalid;
    }
    Failure = Attack->GetMeleeTrajectoryFailure(Mesh.Get());
    if (!Failure.IsEmpty())
    {
        AssetFails(Object,FText::FromString(Failure));
        return EDataValidationResult::Invalid;
    }
    int32 WindowCount = 0;
    for (const FAnimNotifyEvent& Event : Attack->Montage->Notifies)
    {
        if (!Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass.Get())) continue;
        ++WindowCount;
        const auto* Path = Attack->FindMeleeTrajectory(Event);
        if (!Path || !Project_J::CanonicalMeleeAuthoring::ValidateCurrentSource(*Attack, *Mesh, *Path, Failure))
        {
            if (Failure.IsEmpty()) Failure = TEXT("Enabled melee attack is missing a baked notify window.");
            AssetFails(Object,FText::FromString(Failure));
            return EDataValidationResult::Invalid;
        }
        const auto& Keys = Path->Keys;
        if (!Keys.IsEmpty() && !Keys.ContainsByPredicate([&](const auto& Key)
            { return !Key.MeshPosition.Equals(Keys[0].MeshPosition, UE_KINDA_SMALL_NUMBER); }))
        {
            AssetWarning(Object, NSLOCTEXT("ProjectJMeleeTrajectory", "StationaryMeshPath",
                "Every melee key in a window uses the same mesh-space point. Confirm a deliberate stationary probe, or check the selected socket, tip offset, and RAW tracks."));
        }
    }
    if (WindowCount != 1 + Attack->AdditionalMeleeTrajectories.Num())
    {
        AssetFails(Object,FText::FromString(TEXT("Enabled attack needs exactly one baked path per melee notify window.")));
        return EDataValidationResult::Invalid;
    }
    AssetPasses(Object);
    return EDataValidationResult::Valid;
}
