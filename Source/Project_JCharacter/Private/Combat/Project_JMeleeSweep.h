#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AActor;
class UWorld;
struct FHitResult;

namespace Project_J::Combat
{
/** Build once per native dispatch/request; all pieces reuse the same owner/filter metadata. */
FCollisionQueryParams MakeMeleeSweepParams(const AActor* IgnoredOwner);
/** Existing GT melee query policy, shared by visual-pose/canonical producers and current-physics SSR fallback.
 * OutHits, including touches/initial overlaps, is the result; the blocking-hit return is not an admission gate. */
void SweepMeleeSphere(UWorld& World, const FCollisionQueryParams& Params, const FVector& Start, const FVector& End,
 float Radius, TArray<FHitResult>& OutHits);
}
