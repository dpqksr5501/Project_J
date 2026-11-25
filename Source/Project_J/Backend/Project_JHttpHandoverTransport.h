#pragma once

#include "CoreMinimal.h"
#include "Backend/Project_JHandoverTransport.h"
#include "Interfaces/IHttpRequest.h"

/**
 * Production server-to-server HTTP+HMAC-SHA256 handover transport implementing IProject_JHandoverTransport.
 * Signs outgoing envelopes and verifies their integrity and freshness on receipt.
 * Destination-selected actor resolution and replay-safe application belong to UProject_JHandoverManager.
 */
class PROJECT_J_API FProject_JHttpHandoverTransport final
	: public IProject_JHandoverTransport
	, public TSharedFromThis<FProject_JHttpHandoverTransport>
{
public:
	static constexpr int64 MaxTimestampSkewSeconds = 30;

	explicit FProject_JHttpHandoverTransport(FString InSharedSecret, float InRequestTimeoutSeconds = 4.0f);
	virtual ~FProject_JHttpHandoverTransport() override;

	/** Registers a destination zone server URL for TargetNodeId (loopback HTTP or HTTPS only). */
	bool RegisterNodeEndpoint(const FString& NodeId, const FString& EndpointUrl);
	void UnregisterNodeEndpoint(const FString& NodeId);

	// IProject_JHandoverTransport
	virtual bool Send(
		const FProject_JHandoverTransportRequest& Request,
		FProject_JHandoverTransportCallback Completion) override;
	virtual void Cancel(const FGuid& TransferId) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Computes lowercase hex HMAC-SHA256(Secret, Message). */
	static FString ComputeHmacSha256Hex(const FString& Secret, const TArray<uint8>& MessageBytes);

	/** Constant-time ASCII hex comparison to prevent timing side-channels. */
	static bool ConstantTimeEquals(const FString& A, const FString& B);

	/** Serializes a handover envelope to UTF-8 JSON bytes. */
	static bool SerializeEnvelopeJson(const FProject_JHandoverEnvelope& Envelope, TArray<uint8>& OutUtf8Json);

	/** Verifies HMAC-SHA256 signature + timestamp skew and deserializes an incoming handover envelope. */
	static bool VerifyAndParseSignedEnvelope(
		const FString& SharedSecret,
		const FString& SignatureHex,
		int64 TimestampUnixSeconds,
		const TArray<uint8>& BodyUtf8,
		int64 NowUnixSeconds,
		FProject_JHandoverEnvelope& OutEnvelope,
		FString& OutFailureReason);

private:
	static bool IsAllowedEndpointUrl(const FString& Url);

	FString SharedSecret;
	float RequestTimeoutSeconds = 4.0f;
	TMap<FString, FString> NodeEndpoints;
	TMap<FGuid, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> ActiveRequests;
};
