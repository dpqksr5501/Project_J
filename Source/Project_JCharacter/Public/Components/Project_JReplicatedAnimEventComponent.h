#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Components/ActorComponent.h"
#include "Project_JReplicatedAnimEventComponent.generated.h"

class UProject_JLocomotionAnimStateComponent;

UENUM()
enum class EProject_JReplicatedAnimEventType : uint8
{
	MoveStart,
	MoveStop,
	FallOffStart,
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

	void Initialize(UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent);
	void DispatchMoveStarted(bool bWasSprintingForStart);
	void DispatchMoveStopped();
	void DispatchFallOffStarted();
	void DispatchLandingCancelled();

private:
	void DispatchEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag = false);
	void ApplyEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag);
	void ApplyReplicatedEvents(
		const FProject_JReplicatedAnimEventState& CurrentState,
		const FProject_JReplicatedAnimEventState& PreviousState) const;

	// Movement transitions are infrequent state boundaries. Keep them on a dedicated
	// reliable channel so the final stop cannot be lost without making cosmetic events reliable.
	UFUNCTION(Server, Reliable)
	void ServerDispatchMoveState(bool bIsMoving);

	UFUNCTION(Server, Unreliable)
	void ServerDispatchEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag);

	UFUNCTION()
	void OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState);

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAnimEvents)
	FProject_JReplicatedAnimEventState ReplicatedAnimEvents;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JLocomotionAnimStateComponent> LocomotionAnimStateComponent = nullptr;
};
