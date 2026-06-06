#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Components/ActorComponent.h"
#include "Project_JReplicatedAnimEventComponent.generated.h"

class UProject_JLocomotionAnimStateComponent;

/**
 * Centralizes replicated animation event counters and remote application.
 *
 * The replicated state remains on the owning actor to preserve network layout, while this
 * component owns the semantic contract between counters and locomotion event handlers.
 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JReplicatedAnimEventComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JReplicatedAnimEventComponent();

	void MarkMoveStarted(FProject_JReplicatedAnimEventState& State, bool bWasSprintingForStart) const;
	void MarkMoveStopped(FProject_JReplicatedAnimEventState& State) const;
	void MarkJumpStarted(FProject_JReplicatedAnimEventState& State) const;
	void MarkFallOffStarted(FProject_JReplicatedAnimEventState& State) const;
	void MarkLandingCancelled(FProject_JReplicatedAnimEventState& State) const;

	void ApplyReplicatedEvents(
		const FProject_JReplicatedAnimEventState& CurrentState,
		const FProject_JReplicatedAnimEventState& PreviousState,
		UProject_JLocomotionAnimStateComponent* LocomotionAnimStateComponent) const;
};
