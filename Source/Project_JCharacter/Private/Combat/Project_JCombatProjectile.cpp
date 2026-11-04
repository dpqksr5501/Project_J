#include "Combat/Project_JCombatProjectile.h"
#include "Combat/Project_JGameplayAbility_Projectile.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

AProject_JCombatProjectile::AProject_JCombatProjectile()
{
 PrimaryActorTick.bCanEverTick = true;
 bReplicates = true;
 SetReplicateMovement(true);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileRoot")));
 SetActorEnableCollision(false); // Only the explicit authority sweep produces contacts.
}

void AProject_JCombatProjectile::InitializeServer(UProject_JGameplayAbility_Projectile* InExecution,
 uint64 InSerial, const FVector& Direction, float Speed, float Radius, float Lifetime)
{
 if (Serial != 0 || bConsumed) return;
 if (!HasAuthority() || !InExecution || !GetWorld() || InSerial == 0 || Direction.ContainsNaN()
  || Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0.f || !FMath::IsFinite(Radius)
  || Radius <= 0.f || !FMath::IsFinite(Lifetime) || Lifetime <= 0.f) { Retire(); return; }
 Execution = InExecution;
 Serial = InSerial;
 Velocity = Direction.GetSafeNormal() * Speed;
 SweepRadius = Radius;
 ExpiresAt = GetWorld()->GetTimeSeconds() + Lifetime;
}

void AProject_JCombatProjectile::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (!HasAuthority() || bConsumed) return;
 auto* Ability = Execution.Get();
 if (!Ability || !Ability->IsProjectileExecutionCurrent(Serial) || !GetWorld()
  || GetWorld()->GetTimeSeconds() >= ExpiresAt) { Retire(); return; }
 if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
 const FVector Start = GetActorLocation(), End = Start + Velocity * DeltaSeconds;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJProjectile), false, this);
 Params.bFindInitialOverlaps = true;
 Params.AddIgnoredActor(GetOwner());
 FHitResult Hit;
 if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
  FCollisionShape::MakeSphere(SweepRadius), Params))
 {
  // Consume before callbacks. Even immunity or an ineligible blocker consumes this non-piercing shot.
  bConsumed = true;
  SetActorLocation(Hit.Location);
  Ability->ConfirmProjectileHit(this, Serial, Hit.GetActor());
  Destroy();
  return;
 }
 SetActorLocation(End);
}

void AProject_JCombatProjectile::Retire()
{
 bConsumed = true;
 if (HasAuthority()) Destroy();
}

void AProject_JCombatProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
 bConsumed = true;
 if (HasAuthority())
  if (auto* Ability = Execution.Get()) Ability->ProjectileEnded(this, Serial);
 Execution.Reset();
 Super::EndPlay(Reason);
}
