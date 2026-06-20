#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JReplicatedJumpState.h"
#include "Components/ActorComponent.h"
#include "Project_JReplicatedJumpStateComponent.generated.h"

class UProject_JLocomotionAnimStateComponent;

/**
 * Replicates server-confirmed jump presentation state to simulated proxies.
 *
 * The owning client predicts immediately from input; simulated proxies may
 * predict from replicated movement and reconcile when this state arrives.
 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JReplicatedJumpStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JReplicatedJumpStateComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Initialize(UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent);
	void RecordServerConfirmedJump(const FVector& LaunchVelocity);

	const FProject_JReplicatedJumpState& GetJumpState() const { return JumpState; }

private:
	/**
	 * Low-latency visual notification. JumpState remains replicated as the
	 * authoritative recovery path when this cosmetic RPC is dropped.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastConfirmedJump(FProject_JReplicatedJumpState ConfirmedState);

	UFUNCTION()
	void OnRep_JumpState(FProject_JReplicatedJumpState PreviousState);

	void ApplyConfirmedJumpState(const FProject_JReplicatedJumpState& ConfirmedState);
	float ResolveServerStartAgeSeconds(const FProject_JReplicatedJumpState& ConfirmedState) const;
	void BeginUrgentRemoteAnimationUpdate();
	void RestoreRemoteAnimationUpdateRateOptimization();

	UPROPERTY(ReplicatedUsing=OnRep_JumpState)
	FProject_JReplicatedJumpState JumpState;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JLocomotionAnimStateComponent> LocomotionAnimStateComponent = nullptr;

	FTimerHandle RestoreAnimationUpdateRateTimer;
	int32 LastAppliedRemoteJumpSequence = 0;
	bool bUrgentAnimationUpdateActive = false;
	bool bRestoreAnimationUpdateRateOptimization = false;
};
