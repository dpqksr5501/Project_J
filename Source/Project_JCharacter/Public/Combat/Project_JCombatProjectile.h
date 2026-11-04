#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JCombatProjectile.generated.h"
class UProject_JGameplayAbility_Projectile;

/** Server sweeps; clients receive movement only, and never supply contact claims. */
UCLASS()
class PROJECT_JCHARACTER_API AProject_JCombatProjectile : public AActor
{
 GENERATED_BODY()
public:
 AProject_JCombatProjectile();
 void InitializeServer(UProject_JGameplayAbility_Projectile* InExecution, uint64 InSerial,
  const FVector& Direction, float Speed, float Radius, float Lifetime);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 void Retire();
private:
 TWeakObjectPtr<UProject_JGameplayAbility_Projectile> Execution;
 uint64 Serial = 0;
 FVector Velocity = FVector::ZeroVector;
 float SweepRadius = 0.f;
 double ExpiresAt = 0.0;
 bool bConsumed = false;
 friend class FProjectJProjectileLifecycleTest;
};
