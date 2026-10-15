#pragma once
#include "Project_JNPCCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JNPCCallbackFixture.generated.h"

/** Transient native implementations of the same externally callable gameplay events. */
UCLASS(NotBlueprintable, Transient, NotPlaceable)
class AProject_JNPCCallbackFixture : public AProject_JNPCCharacter
{
    GENERATED_BODY()
public:
    mutable TFunction<void()> OnLifeQuery;
    TFunction<void()> OnGrant;
    virtual bool IsDead_Implementation() const override
    {
        auto Callback = MoveTemp(OnLifeQuery);
        if (Callback) { Callback(); }
        return false;
    }
};

UCLASS(NotBlueprintable, Transient)
class UProject_JNPCGrantCallbackFixture : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UProject_JNPCGrantCallbackFixture()
    {
        InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
        NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    }
    virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override
    {
        Super::OnGiveAbility(ActorInfo, Spec);
        auto* NPC = ActorInfo ? Cast<AProject_JNPCCallbackFixture>(ActorInfo->AvatarActor.Get()) : nullptr;
        if (NPC) { auto Callback = MoveTemp(NPC->OnGrant); if (Callback) { Callback(); } }
    }
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override {}
};

UCLASS(NotBlueprintable, Transient)
class UProject_JNPCResultCallbackFixture : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void(AActor*)> OnResult;
    UFUNCTION() void Receive(AActor* Target, double Score)
    {
        const auto Callback = OnResult;
        if (Callback) { Callback(Target); }
    }
};
