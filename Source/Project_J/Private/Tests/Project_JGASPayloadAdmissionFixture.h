#pragma once

#include "CoreMinimal.h"
#include "Project_JGameplayAbility.h"
#include "Project_JGASPayloadAdmissionFixture.generated.h"

/** Actual native synchronous-end producer for the batch continuation regression. No private flags are seeded. */
UCLASS()
class UProject_JGASPayloadImmediateAbility final : public UProject_JGameplayAbility
{
    GENERATED_BODY()
public:
    UProject_JGASPayloadImmediateAbility()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    }
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
    }
};

/** Native independent instances let one granted spec exercise two real concurrent prediction keys. */
UCLASS()
class UProject_JGASPayloadConcurrentAbility final : public UProject_JGameplayAbility
{
    GENERATED_BODY()
public:
    UProject_JGASPayloadConcurrentAbility()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    }
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
    {
        // Remains active until the actual native End; no mirrored flags, payload state or simulated callback.
    }
};

/** Trusted native control runs on an actual local owner from fixture startup. */
UCLASS()
class UProject_JGASPayloadTrustedAbility final : public UProject_JGameplayAbility
{
    GENERATED_BODY()
public:
    UProject_JGASPayloadTrustedAbility()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    }
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
    {
        // Remains active until native End; policy and actual activation info are never overwritten.
    }
};
