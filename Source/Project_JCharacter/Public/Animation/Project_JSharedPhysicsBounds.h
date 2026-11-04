#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/ObjectKey.h"

namespace Project_J::Bounds
{
constexpr int32 MaxSharedBodies=64;

// Actual inputs to the native PA body loop, in native selection order. No shape
// solver or approximate pose hash; views are synchronous and never retained.
struct FBodyValue
{
    FObjectKey Body;
    // Definitions are clean/immutable or rebuilt through native invalidation/GUID.
    // Raw geometry edits without GUID/dirty notification are unsupported.
    FGuid GeometryGuid;
    FName BoneName;
    bool bConsider=false, bPresent=false;
    FTransform Transform=FTransform::Identity;
    bool operator==(const FBodyValue& Other) const
    {
        return Body==Other.Body && GeometryGuid==Other.GeometryGuid && BoneName==Other.BoneName &&
            bConsider==Other.bConsider && bPresent==Other.bPresent &&
            (!bPresent || Transform.Equals(Other.Transform,0.));
    }
};
struct FKey
{
    uint64 Frame=0;
    FObjectKey Source,Owner,World,SourceAsset,PhysicsAsset;
    float BoundsScale=1.f,SourceBoundsScale=1.f;
    uint32 NativeFlags=0;
    int32 DetailMode=0;
    bool operator==(const FKey& Other) const
    {
        return Frame==Other.Frame && Source==Other.Source && Owner==Other.Owner && World==Other.World &&
            SourceAsset==Other.SourceAsset && PhysicsAsset==Other.PhysicsAsset && BoundsScale==Other.BoundsScale &&
            SourceBoundsScale==Other.SourceBoundsScale && NativeFlags==Other.NativeFlags && DetailMode==Other.DetailMode;
    }
};
// Persistent storage is lazy, <=64 body values. No inline KB reservation in each
// default-off source and no UObject lifetime is extended by these object keys.
struct FEntry
{
    FKey Key;
    TArray<FBodyValue> Bodies;
    FBoxSphereBounds LocalBounds=FBoxSphereBounds(FVector::ZeroVector,FVector::ZeroVector,0.);
    bool bValid=false;
};
inline bool SameBodies(TConstArrayView<FBodyValue> A,TConstArrayView<FBodyValue> B)
{
    if(A.Num()!=B.Num()) return false;
    for(int32 I=0;I<A.Num();++I) if(!(A[I]==B[I])) return false;
    return true;
}
}
