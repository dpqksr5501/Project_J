#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JHandoverManager.generated.h"

/**
 * Subsystem to manage seamless server-to-server handovers.
 * Handles the state machine: Pre-Connect -> Ghost Replication -> Authority Switch.
 */
UCLASS()
class PROJECT_J_API UProject_JHandoverManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Initiates a boundary crossing to another server node.
	 * @param ActorToHandover The actor that is crossing the server boundary.
	 * @param TargetServerNodeId The identifier of the destination server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Handover")
	void InitiateHandover(AActor* ActorToHandover, const FString& TargetServerNodeId);

private:
	// Maps an Actor to its current Handover State (e.g., ePreConnect, eGhosting, eSwitching)
	// TMap<AActor*, EHandoverState> HandoverStates;
};
