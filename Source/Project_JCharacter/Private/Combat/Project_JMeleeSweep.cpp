#include "Combat/Project_JMeleeSweep.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "PhysicsEngine/PhysicsSettings.h"

FCollisionQueryParams Project_J::Combat::MakeMeleeSweepParams(const AActor* IgnoredOwner)
{
 check(IsInGameThread());
 static const FName QueryName(TEXT("SphereTraceMultiForObjects"));
 FCollisionQueryParams Params(QueryName, SCENE_QUERY_STAT_ONLY(KismetTraceUtils), false);
 Params.AddIgnoredActor(IgnoredOwner);
 Params.bReturnPhysicalMaterial = true;
 Params.bReturnFaceIndex = !UPhysicsSettings::Get()->bSuppressFaceRemapTable;
 return Params;
}

void Project_J::Combat::SweepMeleeSphere(UWorld& World, const FCollisionQueryParams& Params, const FVector& Start,
 const FVector& End, float Radius, TArray<FHitResult>& OutHits)
{
 check(IsInGameThread());
 OutHits.Reset();
 const FCollisionObjectQueryParams ObjectParams(ECC_Pawn);
 World.SweepMultiByObjectType(OutHits, Start, End, FQuat::Identity, ObjectParams,
  FCollisionShape::MakeSphere(Radius), Params);
}
