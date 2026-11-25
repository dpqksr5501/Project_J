#include "Backend/Project_JNakamaServicePort.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/Base64.h"

void UProject_JNakamaServicePortSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UProject_JGatewaySubsystem>();
	bInitialized = true;
}

void UProject_JNakamaServicePortSubsystem::Deinitialize()
{
	bInitialized = false;
	{
		FScopeLock Lock(&MockMutex);
		MockHandlers.Reset();
		MockReceipts.Reset();
		MockSessionEpochs.Reset();
		CancelledRequests.Reset();
		PendingGatewayCompletions.Reset();
	}
	Super::Deinitialize();
}

void UProject_JNakamaServicePortSubsystem::RegisterMockHandler(
	FName Service,
	FName Operation,
	TFunction<ProjectJ::MMO::FServiceResponse(const ProjectJ::MMO::FServiceRequest&)> Handler)
{
	FScopeLock Lock(&MockMutex);
	MockHandlers.Add(TPair<FName, FName>(Service, Operation), MoveTemp(Handler));
}

int64 UProject_JNakamaServicePortSubsystem::AcquireMockSessionEpoch(const FGuid& CharacterId)
{
	if (!CharacterId.IsValid())
	{
		return 0;
	}
	FScopeLock Lock(&MockMutex);
	int64& Epoch = MockSessionEpochs.FindOrAdd(CharacterId);
	return ++Epoch;
}

bool UProject_JNakamaServicePortSubsystem::IsMockSessionEpochCurrent(const FGuid& CharacterId, int64 Epoch) const
{
	if (!CharacterId.IsValid() || Epoch <= 0)
	{
		return false;
	}
	FScopeLock Lock(&MockMutex);
	const int64* Current = MockSessionEpochs.Find(CharacterId);
	return Current && *Current == Epoch;
}

int32 UProject_JNakamaServicePortSubsystem::GetMockReceiptCount() const
{
	FScopeLock Lock(&MockMutex);
	return MockReceipts.Num();
}

void UProject_JNakamaServicePortSubsystem::Cancel(const FGuid& RequestId)
{
	if (!RequestId.IsValid())
	{
		return;
	}
	FScopeLock Lock(&MockMutex);
	PendingGatewayCompletions.Remove(RequestId);
	if (CancelledRequests.Num() >= 4096)
	{
		CancelledRequests.Reset();
	}
	CancelledRequests.Add(RequestId);
}

void UProject_JNakamaServicePortSubsystem::Submit(
	ProjectJ::MMO::FServiceRequest Request,
	TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion)
{
	if (!bInitialized || !Request.IsValidForDispatch())
	{
		if (Completion)
		{
			ProjectJ::MMO::FServiceResponse Invalid;
			Invalid.RequestId = Request.RequestId;
			Invalid.Status = ProjectJ::MMO::EServiceStatus::Rejected;
			Completion(MoveTemp(Invalid));
		}
		return;
	}

	if (RuntimeMode == EProject_JBackendRuntimeMode::StandaloneMock)
	{
		HandleMockSubmit(MoveTemp(Request), MoveTemp(Completion));
		return;
	}

	HandleGatewaySubmit(MoveTemp(Request), MoveTemp(Completion));
}

void UProject_JNakamaServicePortSubsystem::HandleMockSubmit(
	ProjectJ::MMO::FServiceRequest Request,
	TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion)
{
	ProjectJ::MMO::FServiceResponse Response;
	Response.RequestId = Request.RequestId;

	{
		FScopeLock Lock(&MockMutex);
		if (CancelledRequests.Remove(Request.RequestId) > 0)
		{
			Response.Status = ProjectJ::MMO::EServiceStatus::Cancelled;
		}
		else if (Request.Intent == ProjectJ::MMO::EServiceRequestIntent::Mutation && Request.IdempotencyKey.IsValid())
		{
			if (const FMockReceipt* Existing = MockReceipts.Find(Request.IdempotencyKey))
			{
				const bool bSameRequest = Existing->Service == Request.Service
					&& Existing->Operation == Request.Operation
					&& Existing->RequestPayload == Request.Payload;
				if (bSameRequest)
				{
					Response = Existing->Response;
					Response.RequestId = Request.RequestId;
				}
				else
				{
					// Idempotency key reused with mismatched payload -> fail closed as Conflict.
					Response.Status = ProjectJ::MMO::EServiceStatus::Conflict;
				}
			}
			else if (MockReceipts.Num() >= MaxMockReceipts)
			{
				Response.Status = ProjectJ::MMO::EServiceStatus::Overloaded;
			}
			else if (const auto* Handler = MockHandlers.Find(TPair<FName, FName>(Request.Service, Request.Operation)))
			{
				Response = (*Handler)(Request);
				Response.RequestId = Request.RequestId;
				if (Response.Status == ProjectJ::MMO::EServiceStatus::Success)
				{
					FMockReceipt Receipt;
					Receipt.Service = Request.Service;
					Receipt.Operation = Request.Operation;
					Receipt.RequestPayload = Request.Payload;
					Receipt.Response = Response;
					MockReceipts.Add(Request.IdempotencyKey, MoveTemp(Receipt));
				}
			}
			else
			{
				Response.Status = ProjectJ::MMO::EServiceStatus::Success;
				FMockReceipt Receipt;
				Receipt.Service = Request.Service;
				Receipt.Operation = Request.Operation;
				Receipt.RequestPayload = Request.Payload;
				Receipt.Response = Response;
				MockReceipts.Add(Request.IdempotencyKey, MoveTemp(Receipt));
			}
		}
		else
		{
			if (const auto* Handler = MockHandlers.Find(TPair<FName, FName>(Request.Service, Request.Operation)))
			{
				Response = (*Handler)(Request);
				Response.RequestId = Request.RequestId;
			}
			else
			{
				Response.Status = ProjectJ::MMO::EServiceStatus::Success;
			}
		}
	}

	if (Completion)
	{
		Completion(MoveTemp(Response));
	}
}

void UProject_JNakamaServicePortSubsystem::HandleGatewaySubmit(
	ProjectJ::MMO::FServiceRequest Request,
	TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion)
{
	UGameInstance* GI = GetGameInstance();
	UProject_JGatewaySubsystem* Gateway = GI ? GI->GetSubsystem<UProject_JGatewaySubsystem>() : nullptr;
	if (!Gateway)
	{
		if (Completion)
		{
			ProjectJ::MMO::FServiceResponse Out;
			Out.RequestId = Request.RequestId;
			Out.Status = ProjectJ::MMO::EServiceStatus::Unavailable;
			Completion(MoveTemp(Out));
		}
		return;
	}

	const FString Endpoint = FString::Printf(TEXT("%sj_%s_%s"),
		*NakamaRpcPrefix,
		*Request.Service.ToString().ToLower(),
		*Request.Operation.ToString().ToLower());
	const FString EncodedPayload = FBase64::Encode(Request.Payload);
	const FString JsonBody = FString::Printf(
		TEXT("{\"request_id\":\"%s\",\"idempotency_key\":\"%s\",\"schema_version\":%d,\"payload_b64\":\"%s\"}"),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*Request.IdempotencyKey.ToString(EGuidFormats::DigitsWithHyphensLower),
		Request.SchemaVersion,
		*EncodedPayload);

	FProject_JBackendRequestContext Context;
	Context.RequestId.Value = Request.RequestId;
	Context.IdempotencyKey.Value = Request.IdempotencyKey;
	Context.Intent = (Request.Intent == ProjectJ::MMO::EServiceRequestIntent::Mutation)
		? EProject_JBackendRequestIntent::Mutation
		: EProject_JBackendRequestIntent::Query;

	{
		FScopeLock Lock(&MockMutex);
		if (Completion)
		{
			PendingGatewayCompletions.Add(Request.RequestId, MoveTemp(Completion));
		}
	}

	const TWeakObjectPtr<UProject_JNakamaServicePortSubsystem> WeakThis(this);
	Gateway->DispatchTrackedRequest(Endpoint, JsonBody, Context, [WeakThis](const FProject_JBackendResponseEnvelope& Envelope)
	{
		if (auto* Self = WeakThis.Get()) Self->HandleGatewayEnvelopeResponse(Envelope);
	}, true);
}

void UProject_JNakamaServicePortSubsystem::HandleGatewayEnvelopeResponse(const FProject_JBackendResponseEnvelope& Env)
{
	const FGuid RequestId = Env.RequestContext.RequestId.Value;
	TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion;
	bool bWasCancelled = false;
	{
		FScopeLock Lock(&MockMutex);
		PendingGatewayCompletions.RemoveAndCopyValue(RequestId, Completion);
		bWasCancelled = CancelledRequests.Remove(RequestId) > 0;
	}
	if (!Completion)
	{
		return;
	}

	ProjectJ::MMO::FServiceResponse Resp;
	Resp.RequestId = RequestId;
	if (bWasCancelled)
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::Cancelled;
	}
	else if (Env.bSucceeded)
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::Success;
		FTCHARToUTF8 Utf8(*Env.ResponseData);
		Resp.Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	else if (Env.FailureKind == EProject_JBackendFailureKind::UnknownOutcome || Env.bRequiresReconciliation)
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::UnknownOutcome;
	}
	else if (Env.HttpStatusCode == 409)
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::Conflict;
	}
	else if (Env.FailureKind == EProject_JBackendFailureKind::InvalidRequest
		|| Env.FailureKind == EProject_JBackendFailureKind::DispatchFailed
		|| Env.FailureKind == EProject_JBackendFailureKind::Unavailable)
	{
		// These Gateway results complete before a request is sent. Preserve that
		// distinction from disconnects and ambiguous outcomes after dispatch.
		Resp.Status = ProjectJ::MMO::EServiceStatus::Rejected;
	}
	else if (Env.FailureKind == EProject_JBackendFailureKind::RateLimited || Env.FailureKind == EProject_JBackendFailureKind::Overloaded)
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::Overloaded;
	}
	else
	{
		Resp.Status = ProjectJ::MMO::EServiceStatus::Unavailable;
	}
	Completion(MoveTemp(Resp));
}
