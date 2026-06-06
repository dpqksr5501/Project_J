#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JHandoverManager.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EProject_JHandoverState : uint8
{
	None,
	Preparing,
	Ghosting,
	SwitchingAuthority,
	Completed,
	Failed
};

USTRUCT(BlueprintType)
struct FProject_JHandoverRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	TWeakObjectPtr<AActor> Actor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FString TargetServerNodeId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	EProject_JHandoverState State = EProject_JHandoverState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	int32 PayloadBytes = 0;
};

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

	UFUNCTION(BlueprintPure, Category = "Handover")
	bool IsHandoverInProgress(AActor* Actor) const;

	UFUNCTION(BlueprintCallable, Category = "Handover")
	void CancelHandover(AActor* Actor);

private:
	TMap<TObjectKey<AActor>, FProject_JHandoverRecord> HandoverStates;
};
