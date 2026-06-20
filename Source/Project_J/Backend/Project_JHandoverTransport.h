#pragma once

#include "CoreMinimal.h"
#include "Backend/Project_JHandoverManager.h"
#include "Project_JHandoverTransport.generated.h"

UENUM(BlueprintType)
enum class EProject_JHandoverTransportOutcome : uint8
{
	Accepted,
	Rejected,
	TransportError
};

USTRUCT(BlueprintType)
struct FProject_JHandoverTransportRequest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FProject_JHandoverEnvelope Envelope;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	int32 Attempt = 1;
};

USTRUCT(BlueprintType)
struct FProject_JHandoverTransportResponse
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FGuid TransferId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	int32 Attempt = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	EProject_JHandoverTransportOutcome Outcome = EProject_JHandoverTransportOutcome::TransportError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Handover")
	FString Message;
};

using FProject_JHandoverTransportCallback =
	TFunction<void(const FProject_JHandoverTransportResponse&)>;

/**
 * Server-to-server handover transport port.
 *
 * HTTP, gRPC, message-broker, or platform RPC adapters implement this
 * interface without changing UProject_JHandoverManager.
 */
class PROJECT_J_API IProject_JHandoverTransport
{
public:
	virtual ~IProject_JHandoverTransport() = default;

	virtual bool Send(
		const FProject_JHandoverTransportRequest& Request,
		FProject_JHandoverTransportCallback Completion) = 0;
	virtual void Cancel(const FGuid& TransferId) = 0;
	virtual void Tick(float DeltaSeconds) = 0;
};

/** Deterministic local transport used for development and automation tests. */
class PROJECT_J_API FProject_JLoopbackHandoverTransport final
	: public IProject_JHandoverTransport
	, public TSharedFromThis<FProject_JLoopbackHandoverTransport>
{
public:
	virtual bool Send(
		const FProject_JHandoverTransportRequest& Request,
		FProject_JHandoverTransportCallback Completion) override;
	virtual void Cancel(const FGuid& TransferId) override;
	virtual void Tick(float DeltaSeconds) override;

	void SetResponseOutcome(EProject_JHandoverTransportOutcome InOutcome) { ResponseOutcome = InOutcome; }
	void SetResponseDelay(float InSeconds) { ResponseDelaySeconds = FMath::Max(0.0f, InSeconds); }
	void SetDropResponses(bool bInDropResponses) { bDropResponses = bInDropResponses; }

private:
	struct FPendingResponse
	{
		FProject_JHandoverTransportResponse Response;
		FProject_JHandoverTransportCallback Completion;
		float RemainingDelay = 0.0f;
	};

	EProject_JHandoverTransportOutcome ResponseOutcome = EProject_JHandoverTransportOutcome::Accepted;
	float ResponseDelaySeconds = 0.0f;
	bool bDropResponses = false;
	TArray<FPendingResponse> PendingResponses;
};
