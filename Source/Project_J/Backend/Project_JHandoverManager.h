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

UENUM(BlueprintType)
enum class EProject_JHandoverValidationFailure : uint8
{
	None,
	UnsupportedVersion,
	InvalidTransferId,
	InvalidActorClass,
	InvalidNode,
	InvalidTimestamp,
	PayloadTooLarge,
	ChecksumMismatch
};

USTRUCT(BlueprintType)
struct FProject_JHandoverEnvelope
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FGuid TransferId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FString ActorClassPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	TArray<uint8> Payload;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FDateTime Timestamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FString SourceNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FString TargetNodeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	int32 PayloadChecksum = 0;
};

USTRUCT(BlueprintType)
struct FProject_JHandoverRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	TWeakObjectPtr<AActor> Actor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	EProject_JHandoverState State = EProject_JHandoverState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FProject_JHandoverEnvelope Envelope;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	float ElapsedTime = 0.0f;
};

/**
 * Subsystem to manage seamless server-to-server handovers.
 * Handles the state machine: Pre-Connect -> Ghost Replication -> Authority Switch.
 */
UCLASS(Config=Game, DefaultConfig)
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

	bool BuildEnvelope(AActor* ActorToHandover, const FString& TargetServerNodeId, FProject_JHandoverEnvelope& OutEnvelope) const;
	EProject_JHandoverValidationFailure ValidateEnvelope(const FProject_JHandoverEnvelope& Envelope) const;
	bool ApplyEnvelope(AActor* DestinationActor, const FProject_JHandoverEnvelope& Envelope);
	int32 CalculatePayloadChecksum(const TArray<uint8>& Payload) const;

private:
	TMap<TObjectKey<AActor>, FProject_JHandoverRecord> HandoverStates;

	UPROPERTY()
	TMap<FGuid, FDateTime> AppliedTransferIds;

	UPROPERTY(Config, EditAnywhere, Category = "Handover")
	FString LocalNodeId = TEXT("LocalNode");

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "1"))
	int32 MaxPayloadBytes = 1048576;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "0.0", Units = "s"))
	float MaxEnvelopeAgeSeconds = 30.0f;
};
