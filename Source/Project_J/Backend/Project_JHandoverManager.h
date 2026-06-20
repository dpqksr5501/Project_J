#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JHandoverManager.generated.h"

class AActor;
class IProject_JHandoverTransport;
struct FProject_JHandoverTransportResponse;

UENUM(BlueprintType)
enum class EProject_JHandoverState : uint8
{
	None,
	Preparing,
	Sending,
	AwaitingAck,
	Ghosting,
	SwitchingAuthority,
	Completed,
	Failed,
	Cancelled
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

UENUM(BlueprintType)
enum class EProject_JHandoverFailureReason : uint8
{
	None,
	InvalidEnvelope,
	TransportUnavailable,
	DispatchFailed,
	Rejected,
	TimedOut,
	Cancelled
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
	EProject_JHandoverFailureReason FailureReason = EProject_JHandoverFailureReason::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FProject_JHandoverEnvelope Envelope;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	int32 AttemptCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	float StateElapsedTime = 0.0f;

	float RetryDelayRemaining = 0.0f;
	bool bHasTrackedActor = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FProject_JHandoverStateChanged,
	FGuid,
	TransferId,
	EProject_JHandoverState,
	State,
	EProject_JHandoverFailureReason,
	FailureReason);

/**
 * Coordinates outgoing handover transfers and applies incoming envelopes.
 * Transport details are delegated to IProject_JHandoverTransport.
 */
UCLASS(Config=Game, DefaultConfig)
class PROJECT_J_API UProject_JHandoverManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Handover")
	void InitiateHandover(AActor* ActorToHandover, const FString& TargetServerNodeId);

	/** Starts transport for an already serialized envelope. Useful for server infrastructure adapters. */
	bool StartEnvelopeTransfer(const FProject_JHandoverEnvelope& Envelope, AActor* SourceActor = nullptr);

	UFUNCTION(BlueprintPure, Category = "Handover")
	bool IsHandoverInProgress(AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "Handover")
	EProject_JHandoverState GetHandoverState(const FGuid& TransferId) const;

	UFUNCTION(BlueprintPure, Category = "Handover")
	bool GetHandoverRecord(const FGuid& TransferId, FProject_JHandoverRecord& OutRecord) const;

	UFUNCTION(BlueprintCallable, Category = "Handover")
	void CancelHandover(AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Handover")
	void CancelHandoverByTransferId(const FGuid& TransferId);

	bool BuildEnvelope(AActor* ActorToHandover, const FString& TargetServerNodeId, FProject_JHandoverEnvelope& OutEnvelope) const;
	EProject_JHandoverValidationFailure ValidateEnvelope(const FProject_JHandoverEnvelope& Envelope) const;
	bool ApplyEnvelope(AActor* DestinationActor, const FProject_JHandoverEnvelope& Envelope);
	int32 CalculatePayloadChecksum(const TArray<uint8>& Payload) const;

	void SetTransport(TSharedPtr<IProject_JHandoverTransport> InTransport);
	void TickHandover(float DeltaSeconds);

	UPROPERTY(BlueprintAssignable, Category = "Handover")
	FProject_JHandoverStateChanged OnHandoverStateChanged;

private:
	bool Tick(float DeltaSeconds);
	void DispatchTransfer(const FGuid& TransferId);
	void HandleTransportResponse(const FProject_JHandoverTransportResponse& Response);
	void ScheduleRetry(FProject_JHandoverRecord& Record, EProject_JHandoverFailureReason FailureReason);
	void CompleteTransfer(FProject_JHandoverRecord& Record);
	void FailTransfer(FProject_JHandoverRecord& Record, EProject_JHandoverFailureReason FailureReason);
	void SetRecordState(
		FProject_JHandoverRecord& Record,
		EProject_JHandoverState NewState,
		EProject_JHandoverFailureReason FailureReason = EProject_JHandoverFailureReason::None);
	bool HasActiveTransferForActor(const AActor* Actor) const;
	void PruneReplayWindow();

	TMap<FGuid, FProject_JHandoverRecord> HandoverStates;
	TMap<FGuid, FDateTime> AppliedTransferIds;
	TSharedPtr<IProject_JHandoverTransport> Transport;
	FTSTicker::FDelegateHandle TickHandle;

	UPROPERTY(Config, EditAnywhere, Category = "Handover")
	FString LocalNodeId = TEXT("LocalNode");

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "1"))
	int32 MaxPayloadBytes = 1048576;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "0.0", Units = "s"))
	float MaxEnvelopeAgeSeconds = 30.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "0.05", Units = "s"))
	float AckTimeoutSeconds = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "0"))
	int32 MaxRetryCount = 2;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "0.0", Units = "s"))
	float RetryDelaySeconds = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Handover", meta = (ClampMin = "1.0", Units = "s"))
	float TerminalRecordTtlSeconds = 300.0f;
};
