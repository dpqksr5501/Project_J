#pragma once
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatProjectile.h"
#include "Project_JProjectileAttackDefinition.generated.h"

/** One straight, non-piercing server projectile. The existing style catalog owns this definition. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JProjectileAttackDefinition : public UProject_JAttackDefinition
{
 GENERATED_BODY()
public:
 UProject_JProjectileAttackDefinition() { bMontageDriven = false; }
 /** Optional authored cosmetic subclass; authoritative motion/contact remain native. */
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
 TSubclassOf<AProject_JCombatProjectile> ProjectileClass = AProject_JCombatProjectile::StaticClass();
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile", meta=(ClampMin="1", ClampMax="100000"))
 float Speed = 2000.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile", meta=(ClampMin="1", ClampMax="100"))
 float Radius = 8.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile", meta=(ClampMin="0.01", ClampMax="10"))
 float Lifetime = 3.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile", meta=(ClampMin="0.01", ClampMax="5"))
 float Windup = 0.2f;
 bool IsProjectileValid() const
 {
  return AttackTag.IsValid() && DamageEffect && ProjectileClass && FMath::IsFinite(Speed) && Speed >= 1.f && Speed <= 100000.f
   && FMath::IsFinite(Radius) && Radius >= 1.f && Radius <= 100.f
   && FMath::IsFinite(Lifetime) && Lifetime >= .01f && Lifetime <= 10.f
   && FMath::IsFinite(Windup) && Windup >= .01f && Windup <= 5.f;
 }
};
