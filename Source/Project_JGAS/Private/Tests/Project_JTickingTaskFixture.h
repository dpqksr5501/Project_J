#pragma once

#include "GameplayTask.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JTickingTaskFixture.generated.h"

/** Asset-independent test task, never created by gameplay. */
UCLASS(Transient, NotBlueprintable)
class UProject_JTickingTaskFixture : public UGameplayTask
{
	GENERATED_BODY()
public:
	UProject_JTickingTaskFixture(const FObjectInitializer& Initializer = FObjectInitializer::Get()) : Super(Initializer)
	{
		bTickingTask = true;
	}
	virtual void TickTask(float DeltaTime) override { ++TickCount; }
	void InitializeForTest(IGameplayTaskOwnerInterface& Owner) { InitTask(Owner, 127); }
	int32 TickCount = 0;
};

/** Exercises real GAS completion without an authored montage or a timer. */
UCLASS(Transient, NotBlueprintable)
class UProject_JEndedAbilityFixture : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UProject_JEndedAbilityFixture() { InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; }
    virtual void ActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*, FGameplayAbilityActivationInfo, const FGameplayEventData*) override {}
    void CompleteForTest() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false); }
};
