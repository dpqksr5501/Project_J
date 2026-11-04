#pragma once
#include "CoreMinimal.h"
#include "Project_JGameplayAbility.h"
#include "Project_JGameplayAbility_Projectile.generated.h"
class UProject_JProjectileAttackDefinition;
class UProject_JEquipmentRuntimeComponent;
class UProject_JCombatStyleDefinition;
class AProject_JCombatProjectile;

/** Server-only GAS attack. Equipment/style grants and AttackTag select authored data. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JGameplayAbility_Projectile : public UProject_JGameplayAbility
{
 GENERATED_BODY()
public:
 UProject_JGameplayAbility_Projectile();
 virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
  const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
  FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
 virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
  FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
 virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
  FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
 bool IsProjectileExecutionCurrent(uint64 ExpectedSerial) const;
 bool ConfirmProjectileHit(AProject_JCombatProjectile* Projectile, uint64 ExpectedSerial, AActor* Target);
 void ProjectileEnded(AProject_JCombatProjectile* Projectile, uint64 ExpectedSerial);
protected:
 UPROPERTY(EditDefaultsOnly, Category="Combat")
 FGameplayTag AttackTag;
private:
 friend class FProjectJProjectileLifecycleTest;
 const UProject_JProjectileAttackDefinition* ResolveDefinition(const FGameplayAbilityActorInfo* ActorInfo) const;
 void Launch();
 void CancelExecution();
 UPROPERTY(Transient) TObjectPtr<const UProject_JProjectileAttackDefinition> Definition;
 UPROPERTY(Transient) TObjectPtr<const UProject_JCombatStyleDefinition> StartedStyle;
 TWeakObjectPtr<UProject_JEquipmentRuntimeComponent> Equipment;
 TWeakObjectPtr<AProject_JCombatProjectile> ActiveProjectile;
 TWeakObjectPtr<AActor> StartedAvatar;
 uint64 Serial = 0;
 uint64 WeaponRevision = 0;
 uint64 ASCEpoch = 0;
 bool bEnding = false;
 bool bHitSubmitted = false;
 FDelegateHandle WeaponRevokedHandle;
 FTimerHandle LaunchTimer;
};
