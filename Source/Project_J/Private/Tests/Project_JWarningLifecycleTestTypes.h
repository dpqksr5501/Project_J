#pragma once

#include "Abilities/GameplayAbility.h"
#include "Game/Project_JPlayerController.h"
#include "Project_JWarningLifecycleTestTypes.generated.h"

UCLASS(Transient)
class AProject_JWarningPlayerController : public AProject_JPlayerController
{
	GENERATED_BODY()
public:
	AProject_JWarningPlayerController()=default;
};

/** Observes the real GAS cancellation contract without requiring an RPC mock. */
UCLASS(Transient)
class UProject_JWarningCancellationAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UProject_JWarningCancellationAbility()
	{
		InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
		NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
	}
	bool bLastReplicateCancel=false;
	int32 CancelCount=0;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle,const FGameplayAbilityActorInfo*,FGameplayAbilityActivationInfo,const FGameplayEventData*) override {}
	virtual void CancelAbility(FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,FGameplayAbilityActivationInfo Activation,bool bReplicate) override
	{
		bLastReplicateCancel=bReplicate; ++CancelCount;
		Super::CancelAbility(Handle,Info,Activation,bReplicate);
	}
};
