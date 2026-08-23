#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Components/ActorComponent.h"
#include "Project_JReplicatedAnimEventComponent.generated.h"

class UProject_JLocomotionAnimStateComponent;
class UProject_JAnimationUpdateCoordinatorComponent;

UENUM()
enum class EProject_JReplicatedAnimEventType : uint8
{
	MoveStart,
	MoveStop,
	FallOffStart,
	LandingStart,
	LandingCancel
};

/**
 * Centralizes replicated animation event counters and remote application.
 *
 * The component owns the replicated state and the semantic contract between counters and
 * locomotion event handlers.
 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JReplicatedAnimEventComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JReplicatedAnimEventComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Initialize(
		UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent,
		UProject_JAnimationUpdateCoordinatorComponent* InAnimationUpdateCoordinator);
	void DispatchMoveStarted(bool bWasSprintingForStart);
	void DispatchMoveStopped(bool bWasSprintingAtStop);
	void DispatchFallOffStarted();
	void DispatchLandingStarted(float ImpactSpeed, bool bWasMoving, bool bWasSprinting, bool bWasHeavy);
	void DispatchLandingCancelled();

private:
	void DispatchEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag = false);
	void ApplyEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag);
	void ApplyMoveEvent(bool bIsMoving, bool bWasSprintingAtBoundary);
	void ApplyLandingStart(float ImpactSpeed, bool bWasMoving, bool bWasSprinting, bool bWasHeavy);
	void ApplyLandingCancel();
	void ReplicateLatestState();
	void ApplyReplicatedEvents(
		const FProject_JReplicatedAnimEventState& CurrentState,
		const FProject_JReplicatedAnimEventState& PreviousState);
	void ApplyRemoteMoveState(const FProject_JReplicatedAnimEventState& State);
	void ApplyRemoteLandingState(const FProject_JReplicatedAnimEventState& State);
	void ApplyRemoteFallOffState(const FProject_JReplicatedAnimEventState& State);
	float ResolveServerEventAgeSeconds(float ServerTimeSeconds) const;
	void RequestUrgentRemoteAnimationUpdate() const;

	// Movement transitions are infrequent state boundaries. Keep them on a dedicated
	// reliable channel so the final stop cannot be lost without making cosmetic events reliable.
	UFUNCTION(Server, Reliable)
	void ServerDispatchMoveState(bool bIsMoving, bool bWasSprintingAtBoundary);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastAnimEventState(FProject_JReplicatedAnimEventState EventState);

	// These are rare semantic boundaries. Reliable delivery keeps cancellation
	// ordered with reliable movement edges on the owning actor channel.
	UFUNCTION(Server, Reliable)
	void ServerDispatchEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag);

	UFUNCTION()
	void OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState);

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAnimEvents)
	FProject_JReplicatedAnimEventState ReplicatedAnimEvents;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JLocomotionAnimStateComponent> LocomotionAnimStateComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JAnimationUpdateCoordinatorComponent> AnimationUpdateCoordinator = nullptr;

	int32 NextSemanticEventOrder = 0;
	int32 LastAppliedMoveSequence = 0;
	int32 LastAppliedLandingRevision = 0;
	int32 LastAppliedFallOffCounter = 0;
	int32 LastAppliedSemanticEventOrder = 0;
};
