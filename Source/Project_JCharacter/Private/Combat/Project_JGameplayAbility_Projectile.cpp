#include "Combat/Project_JGameplayAbility_Projectile.h"
#include "Combat/Project_JProjectileAttackDefinition.h"
#include "Combat/Project_JCombatProjectile.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JCombatHitValidation.h"
#include "Combat/Project_JAuthoritativeDamage.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

UProject_JGameplayAbility_Projectile::UProject_JGameplayAbility_Projectile()
{
 InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
 NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
 FGameplayTagContainer Tags; Tags.AddTag(FProject_JGameplayTags::Get().State_Attacking); SetAssetTags(Tags);
 ActivationOwnedTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
 ActivationRequiredTags.AddTag(FProject_JGameplayTags::Get().State_CombatMode);
}

const UProject_JProjectileAttackDefinition* UProject_JGameplayAbility_Projectile::ResolveDefinition(
 const FGameplayAbilityActorInfo* ActorInfo) const
{
 const auto* Player = ActorInfo ? Cast<AProject_JPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
 const auto* Style = Player ? Player->GetCombatConfiguration().GameplayStyle.Get() : nullptr;
 const auto* Set = Style ? Style->GetRuntimeAttackSet() : nullptr;
 return Set ? Cast<UProject_JProjectileAttackDefinition>(Set->FindAttack(AttackTag)) : nullptr;
}

bool UProject_JGameplayAbility_Projectile::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
 const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
 const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
 const auto* Avatar = ActorInfo ? Cast<AProject_JPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
 const auto* Runtime = Avatar ? Avatar->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
 const auto* Attack = ResolveDefinition(ActorInfo);
 const auto* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
 if (!Avatar || !Runtime || Runtime->GetWeaponRevision() == 0 || !Attack || !Attack->IsProjectileValid()
  || !Project_J::Combat::CanPlayerMeleeAttack(Avatar) || !ASC || bEnding
  || ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)) return false;
 FGameplayTagContainer Owned; ASC->GetOwnedGameplayTags(Owned);
 if (!Owned.HasAll(Attack->RequiredOwnerTags) || Owned.HasAny(Attack->BlockedOwnerTags)) return false;
 return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UProject_JGameplayAbility_Projectile::ActivateAbility(FGameplayAbilitySpecHandle Handle,
 const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
 const FGameplayEventData* TriggerEventData)
{
 auto* Avatar = ActorInfo ? Cast<AProject_JPlayerCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
 Definition = ResolveDefinition(ActorInfo);
 Equipment = Avatar ? Avatar->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
 const auto* ASC = ActorInfo ? Cast<UProject_JAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
 if (!Avatar || !Avatar->HasAuthority() || !ASC || !Definition || !Definition->IsProjectileValid()
  || !Equipment.IsValid() || Equipment->GetWeaponRevision() == 0)
 { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
 ++Serial; if (!Serial) ++Serial;
 StartedAvatar = Avatar;
 StartedStyle = Avatar->GetCombatConfiguration().GameplayStyle;
 WeaponRevision = Equipment->GetWeaponRevision(); ASCEpoch = ASC->GetActorInfoRevision();
 bEnding = false; bHitSubmitted = false;
 WeaponRevokedHandle = Equipment->OnWeaponRevoked().AddUObject(this, &ThisClass::CancelExecution);
 const uint64 Expected = Serial;
 if (!IsProjectileExecutionCurrent(Expected) || !CommitAbility(Handle, ActorInfo, ActivationInfo)
  || !IsProjectileExecutionCurrent(Expected))
 { if (Serial == Expected && IsActive()) CancelExecution(); return; }
 GetWorld()->GetTimerManager().SetTimer(LaunchTimer, this, &ThisClass::Launch, Definition->Windup, false);
}

bool UProject_JGameplayAbility_Projectile::IsProjectileExecutionCurrent(uint64 ExpectedSerial) const
{
 const auto* Avatar = Cast<AProject_JPlayerCharacter>(StartedAvatar.Get());
 const auto* ASC = CurrentActorInfo ? Cast<UProject_JAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent.Get()) : nullptr;
 if (!CanReceiveClientExecutionData() || bEnding || ExpectedSerial == 0 || ExpectedSerial != Serial
  || !Avatar || !Avatar->HasAuthority() || GetAvatarActorFromActorInfo() != Avatar || !ASC
  || ASC->GetAvatarActor() != Avatar || ASC->GetActorInfoRevision() != ASCEpoch
  || !Equipment.IsValid() || WeaponRevision == 0 || Equipment->GetWeaponRevision() != WeaponRevision
  || !Definition || !Project_J::Combat::CanPlayerMeleeAttack(Avatar)) return false;
 FGameplayTagContainer Owned; ASC->GetOwnedGameplayTags(Owned);
 if (!Owned.HasAll(Definition->RequiredOwnerTags) || Owned.HasAny(Definition->BlockedOwnerTags)) return false;
 const auto* CurrentStyle = Avatar->GetCombatConfiguration().GameplayStyle.Get();
 return StartedStyle && !FProject_JCombatConfiguration::ShouldCancelExecution(
  StartedStyle->ExecutionChangePolicy, StartedStyle, CurrentStyle);
}

void UProject_JGameplayAbility_Projectile::Launch()
{
 const uint64 Expected = Serial;
 if (!IsProjectileExecutionCurrent(Expected)) { CancelExecution(); return; }
 if (ActiveProjectile.IsValid()) return;
 GetWorld()->GetTimerManager().ClearTimer(LaunchTimer);
 AActor* Avatar = StartedAvatar.Get();
 const FVector Direction = Avatar->GetActorForwardVector(); // Server-owned facing; no client contact/aim payload.
 FActorSpawnParameters Params; Params.Owner = Avatar; Params.Instigator = Cast<APawn>(Avatar);
 Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 // Start at the avatar center, ignoring only the source. Never offset past a nearby wall.
 auto* Projectile = GetWorld()->SpawnActor<AProject_JCombatProjectile>(
  Definition->ProjectileClass, Avatar->GetActorLocation(), Direction.Rotation(), Params);
 if (!IsValid(Projectile) || Projectile->IsActorBeingDestroyed()) { CancelExecution(); return; }
 if (!IsProjectileExecutionCurrent(Expected)) { Projectile->Retire(); return; }
 ActiveProjectile = Projectile;
 Projectile->InitializeServer(this, Expected, Direction, Definition->Speed, Definition->Radius, Definition->Lifetime);
}

bool UProject_JGameplayAbility_Projectile::ConfirmProjectileHit(AProject_JCombatProjectile* Projectile,
 uint64 ExpectedSerial, AActor* Target)
{
 if (!Projectile || ActiveProjectile.Get() != Projectile || bHitSubmitted || !IsProjectileExecutionCurrent(ExpectedSerial)) return false;
 bHitSubmitted = true; // Reserve before GAS callbacks, never retry a single-impact projectile.
 TStrongObjectPtr<UProject_JGameplayAbility_Projectile> KeepAlive(this);
 return Project_J::Combat::ApplyAuthoritativeDamage(StartedAvatar.Get(), Target, Definition->DamageEffect,
  [this, Projectile, ExpectedSerial]() { return ActiveProjectile.Get() == Projectile && IsProjectileExecutionCurrent(ExpectedSerial); });
}

void UProject_JGameplayAbility_Projectile::ProjectileEnded(AProject_JCombatProjectile* Projectile, uint64 ExpectedSerial)
{
 if (Serial != ExpectedSerial || ActiveProjectile.Get() != Projectile || bEnding) return;
 ActiveProjectile.Reset();
 EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UProject_JGameplayAbility_Projectile::CancelExecution()
{
 if (IsActive() && !bEnding) EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UProject_JGameplayAbility_Projectile::EndAbility(FGameplayAbilitySpecHandle Handle,
 const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
 bool bReplicateEndAbility, bool bWasCancelled)
{
 if (bEnding) return;
 bEnding = true;
 if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(LaunchTimer);
 if (Equipment.IsValid()) Equipment->OnWeaponRevoked().Remove(WeaponRevokedHandle);
 WeaponRevokedHandle.Reset();
 auto* Projectile = ActiveProjectile.Get(); ActiveProjectile.Reset();
 if (Projectile) Projectile->Retire();
 Definition = nullptr; StartedStyle = nullptr; Equipment.Reset(); StartedAvatar.Reset(); WeaponRevision = 0;
 Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
 bEnding = false;
}
