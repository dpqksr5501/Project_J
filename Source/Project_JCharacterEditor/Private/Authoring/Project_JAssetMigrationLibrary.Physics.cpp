#include "Authoring/Project_JAssetMigrationLibrary.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

int32 UProject_JAssetMigrationLibrary::RemoveEmptyPhysicsAssetBodies(UPhysicsAsset* Asset)
{
    if (!IsValid(Asset) || Asset->SkeletalBodySetups.IsEmpty()) return INDEX_NONE;

    TArray<int32> EmptyBodies;
    for (int32 Index = 0; Index < Asset->SkeletalBodySetups.Num(); ++Index)
    {
        const USkeletalBodySetup* Body = Asset->SkeletalBodySetups[Index];
        if (!IsValid(Body) || Body->GetOuter() != Asset) return INDEX_NONE;
        if (Body->AggGeom.GetElementCount() == 0) EmptyBodies.Add(Index);
    }
    if (EmptyBodies.IsEmpty()) return 0;
    if (EmptyBodies.Num() == Asset->SkeletalBodySetups.Num()) return INDEX_NONE;

    Asset->Modify();
    for (int32 Index = EmptyBodies.Num() - 1; Index >= 0; --Index)
    {
        FPhysicsAssetUtils::DestroyBody(Asset, EmptyBodies[Index]);
    }
    Asset->PostEditChange();
    return EmptyBodies.Num();
}
