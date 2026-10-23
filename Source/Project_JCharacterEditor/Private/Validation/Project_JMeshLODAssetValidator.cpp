#include "Validation/Project_JMeshLODAssetValidator.h"
#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Authoring/Project_JMeshLODSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/DataValidation.h"

bool UProject_JMeshLODAssetValidator::CanValidateAsset_Implementation(const FAssetData&,UObject* Asset,FDataValidationContext&) const
{
    if(!Asset || (!Asset->IsA<USkeletalMesh>() && !Asset->IsA<UStaticMesh>()))return false;
    const auto* Policy=ProjectJ::MeshLODAuthoring::LoadPolicy();
    return Policy && Policy->ValidationRoots.ContainsByPredicate([&](const FString& Root){return Asset->GetOutermost()->GetName().StartsWith(Root);});
}
EDataValidationResult UProject_JMeshLODAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&,UObject* Asset,FDataValidationContext& Context)
{
    using namespace ProjectJ::MeshLODAuthoring;
    const auto* Policy=LoadPolicy();if(!Policy)return EDataValidationResult::NotValidated;
    const auto R=Inspect(Asset,*Policy,ERole::Unclassified);
    if(R.Status==EStatus::Invalid){AssetFails(Asset,FText::FromString(R.Reason));return EDataValidationResult::Invalid;}
    if(R.Status!=EStatus::Existing)
    {
        const FText Message=FText::FromString(FString::Printf(TEXT("Mesh LOD: %s Use Project J Mesh LOD inspection; no assets are generated on import/validation."),*R.Reason));
        if(R.bExplicitNativeWorkflow)Context.AddMessage(EMessageSeverity::Info,Message);
        else Context.AddWarning(Message);
    }
    AssetPasses(Asset);return EDataValidationResult::Valid;
}
