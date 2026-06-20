#include "Backend/Project_JHandoverManager.h"
#include "Backend/Project_JHandoverTransport.h"
#include "GameFramework/Actor.h"
#include "Misc/Crc.h"
#include "Network/Project_JHandoverSerializable.h"
#include "Project_J.h"

void UProject_JHandoverManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UProject_JHandoverManager::Tick));
}

void UProject_JHandoverManager::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	HandoverStates.Reset();
	AppliedTransferIds.Reset();
	Super::Deinitialize();
}

void UProject_JHandoverManager::InitiateHandover(AActor* ActorToHandover, const FString& TargetServerNodeId)
{
	if (!ActorToHandover || TargetServerNodeId.IsEmpty())
	{
		return;
	}

	if (!ActorToHandover->HasAuthority())
	{
		UE_LOG(LogProject_J, Warning, TEXT("Ignoring handover for %s because this node does not have authority."), *GetNameSafe(ActorToHandover));
		return;
	}

	if (!Cast<IProject_JHandoverSerializable>(ActorToHandover))
	{
		UE_LOG(LogProject_J, Warning, TEXT("Actor %s does not implement Project_JHandoverSerializable."), *GetNameSafe(ActorToHandover));
		return;
	}

	FProject_JHandoverEnvelope Envelope;
	if (!BuildEnvelope(ActorToHandover, TargetServerNodeId, Envelope))
	{
		UE_LOG(LogProject_J, Warning, TEXT("Failed to build handover envelope for %s."), *GetNameSafe(ActorToHandover));
		return;
	}

	StartEnvelopeTransfer(Envelope, ActorToHandover);
}

bool UProject_JHandoverManager::StartEnvelopeTransfer(const FProject_JHandoverEnvelope& Envelope, AActor* SourceActor)
{
	if (!Envelope.TransferId.IsValid())
	{
		return false;
	}
	if (HandoverStates.Contains(Envelope.TransferId))
	{
		return false;
	}
	if (SourceActor && HasActiveTransferForActor(SourceActor))
	{
		return false;
	}

	FProject_JHandoverRecord& Record = HandoverStates.Add(Envelope.TransferId);
	Record.Actor = SourceActor;
	Record.bHasTrackedActor = (SourceActor != nullptr);
	Record.Envelope = Envelope;
	Record.State = EProject_JHandoverState::None;
	if (ValidateEnvelope(Envelope) != EProject_JHandoverValidationFailure::None)
	{
		SetRecordState(
			Record,
			EProject_JHandoverState::Failed,
			EProject_JHandoverFailureReason::InvalidEnvelope);
		return false;
	}
	SetRecordState(Record, EProject_JHandoverState::Preparing);

	DispatchTransfer(Envelope.TransferId);
	return true;
}

bool UProject_JHandoverManager::IsHandoverInProgress(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	for (const auto& Pair : HandoverStates)
	{
		const FProject_JHandoverRecord& Record = Pair.Value;
		if (Record.Actor.Get() == Actor)
		{
			return Record.State == EProject_JHandoverState::Preparing ||
				Record.State == EProject_JHandoverState::Sending ||
				Record.State == EProject_JHandoverState::AwaitingAck ||
				Record.State == EProject_JHandoverState::Ghosting ||
				Record.State == EProject_JHandoverState::SwitchingAuthority;
		}
	}
	return false;
}

EProject_JHandoverState UProject_JHandoverManager::GetHandoverState(const FGuid& TransferId) const
{
	if (const FProject_JHandoverRecord* Record = HandoverStates.Find(TransferId))
	{
		return Record->State;
	}
	return EProject_JHandoverState::None;
}

bool UProject_JHandoverManager::GetHandoverRecord(const FGuid& TransferId, FProject_JHandoverRecord& OutRecord) const
{
	if (const FProject_JHandoverRecord* Record = HandoverStates.Find(TransferId))
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

void UProject_JHandoverManager::CancelHandover(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	TArray<FGuid> TransfersToCancel;
	for (const auto& Pair : HandoverStates)
	{
		if (Pair.Value.Actor.Get() == Actor)
		{
			TransfersToCancel.Add(Pair.Key);
		}
	}
	for (const FGuid& TransferId : TransfersToCancel)
	{
		CancelHandoverByTransferId(TransferId);
	}
}

void UProject_JHandoverManager::CancelHandoverByTransferId(const FGuid& TransferId)
{
	FProject_JHandoverRecord* Record = HandoverStates.Find(TransferId);
	if (!Record ||
		Record->State == EProject_JHandoverState::Completed ||
		Record->State == EProject_JHandoverState::Failed ||
		Record->State == EProject_JHandoverState::Cancelled)
	{
		return;
	}

	if (Transport)
	{
		Transport->Cancel(TransferId);
	}
	SetRecordState(*Record, EProject_JHandoverState::Cancelled, EProject_JHandoverFailureReason::Cancelled);
}

bool UProject_JHandoverManager::BuildEnvelope(
	AActor* ActorToHandover,
	const FString& TargetServerNodeId,
	FProject_JHandoverEnvelope& OutEnvelope) const
{
	IProject_JHandoverSerializable* SerializableActor = Cast<IProject_JHandoverSerializable>(ActorToHandover);
	if (!SerializableActor || TargetServerNodeId.IsEmpty())
	{
		return false;
	}

	TArray<uint8> Payload;
	SerializableActor->SerializeForHandover(Payload);
	if (Payload.Num() > FMath::Max(1, MaxPayloadBytes))
	{
		return false;
	}

	OutEnvelope.Version = 1;
	OutEnvelope.TransferId = FGuid::NewGuid();
	OutEnvelope.ActorClassPath = ActorToHandover->GetClass()->GetPathName();
	OutEnvelope.Payload = MoveTemp(Payload);
	OutEnvelope.Timestamp = FDateTime::UtcNow();
	OutEnvelope.SourceNodeId = LocalNodeId;
	OutEnvelope.TargetNodeId = TargetServerNodeId;
	OutEnvelope.PayloadChecksum = CalculatePayloadChecksum(OutEnvelope.Payload);
	return ValidateEnvelope(OutEnvelope) == EProject_JHandoverValidationFailure::None;
}

EProject_JHandoverValidationFailure UProject_JHandoverManager::ValidateEnvelope(
	const FProject_JHandoverEnvelope& Envelope) const
{
	if (Envelope.Version != 1)
	{
		return EProject_JHandoverValidationFailure::UnsupportedVersion;
	}
	if (!Envelope.TransferId.IsValid())
	{
		return EProject_JHandoverValidationFailure::InvalidTransferId;
	}
	if (Envelope.ActorClassPath.IsEmpty())
	{
		return EProject_JHandoverValidationFailure::InvalidActorClass;
	}
	if (Envelope.SourceNodeId.IsEmpty() || Envelope.TargetNodeId.IsEmpty())
	{
		return EProject_JHandoverValidationFailure::InvalidNode;
	}
	if (Envelope.Timestamp == FDateTime::MinValue())
	{
		return EProject_JHandoverValidationFailure::InvalidTimestamp;
	}

	const FTimespan EnvelopeAge = FDateTime::UtcNow() - Envelope.Timestamp;
	if (EnvelopeAge.GetTotalSeconds() < -3.0 ||
		(MaxEnvelopeAgeSeconds > 0.0f && EnvelopeAge.GetTotalSeconds() > MaxEnvelopeAgeSeconds))
	{
		return EProject_JHandoverValidationFailure::InvalidTimestamp;
	}
	if (Envelope.Payload.Num() > FMath::Max(1, MaxPayloadBytes))
	{
		return EProject_JHandoverValidationFailure::PayloadTooLarge;
	}
	if (Envelope.PayloadChecksum != CalculatePayloadChecksum(Envelope.Payload))
	{
		return EProject_JHandoverValidationFailure::ChecksumMismatch;
	}

	return EProject_JHandoverValidationFailure::None;
}

bool UProject_JHandoverManager::ApplyEnvelope(
	AActor* DestinationActor,
	const FProject_JHandoverEnvelope& Envelope)
{
	if (!DestinationActor || !DestinationActor->HasAuthority())
	{
		return false;
	}
	if (ValidateEnvelope(Envelope) != EProject_JHandoverValidationFailure::None)
	{
		return false;
	}

	PruneReplayWindow();
	if (AppliedTransferIds.Contains(Envelope.TransferId) ||
		Envelope.TargetNodeId != LocalNodeId ||
		Envelope.ActorClassPath != DestinationActor->GetClass()->GetPathName())
	{
		return false;
	}

	IProject_JHandoverSerializable* SerializableActor = Cast<IProject_JHandoverSerializable>(DestinationActor);
	if (!SerializableActor)
	{
		return false;
	}

	SerializableActor->DeserializeFromHandover(Envelope.Payload);
	AppliedTransferIds.Add(Envelope.TransferId, FDateTime::UtcNow());
	return true;
}

int32 UProject_JHandoverManager::CalculatePayloadChecksum(const TArray<uint8>& Payload) const
{
	return static_cast<int32>(FCrc::MemCrc32(Payload.GetData(), Payload.Num()));
}

void UProject_JHandoverManager::SetTransport(TSharedPtr<IProject_JHandoverTransport> InTransport)
{
	Transport = MoveTemp(InTransport);
}

void UProject_JHandoverManager::TickHandover(float DeltaSeconds)
{
	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	if (Transport)
	{
		Transport->Tick(SafeDeltaSeconds);
	}

	TArray<FGuid> TransfersToDispatch;
	TArray<FGuid> TransfersToTimeout;
	TArray<FGuid> RecordsToRemove;

	for (auto& Pair : HandoverStates)
	{
		FProject_JHandoverRecord& Record = Pair.Value;
		Record.StateElapsedTime += SafeDeltaSeconds;

		// Cancel active transport before retaining a lightweight terminal record.
		if (Record.bHasTrackedActor && !Record.Actor.IsValid() &&
			Record.State != EProject_JHandoverState::Completed &&
			Record.State != EProject_JHandoverState::Failed &&
			Record.State != EProject_JHandoverState::Cancelled)
		{
			if (Transport)
			{
				Transport->Cancel(Pair.Key);
			}
			SetRecordState(
				Record,
				EProject_JHandoverState::Cancelled,
				EProject_JHandoverFailureReason::Cancelled);
			continue;
		}

		// Retain lightweight terminal metadata for diagnostics, then expire it.
		if (Record.State == EProject_JHandoverState::Completed ||
			Record.State == EProject_JHandoverState::Failed ||
			Record.State == EProject_JHandoverState::Cancelled)
		{
			if (Record.StateElapsedTime >= FMath::Max(1.0f, TerminalRecordTtlSeconds))
			{
				RecordsToRemove.Add(Pair.Key);
			}
			continue;
		}

		if (Record.State == EProject_JHandoverState::Preparing && Record.RetryDelayRemaining > 0.0f)
		{
			Record.RetryDelayRemaining -= SafeDeltaSeconds;
			if (Record.RetryDelayRemaining <= 0.0f)
			{
				TransfersToDispatch.Add(Pair.Key);
			}
		}
		else if (Record.State == EProject_JHandoverState::AwaitingAck &&
			Record.StateElapsedTime >= FMath::Max(0.05f, AckTimeoutSeconds))
		{
			TransfersToTimeout.Add(Pair.Key);
		}
	}

	for (const FGuid& TransferId : RecordsToRemove)
	{
		HandoverStates.Remove(TransferId);
	}
	for (const FGuid& TransferId : TransfersToDispatch)
	{
		DispatchTransfer(TransferId);
	}
	for (const FGuid& TransferId : TransfersToTimeout)
	{
		if (FProject_JHandoverRecord* Record = HandoverStates.Find(TransferId))
		{
			if (Transport)
			{
				Transport->Cancel(TransferId);
			}
			ScheduleRetry(*Record, EProject_JHandoverFailureReason::TimedOut);
		}
	}

	PruneReplayWindow();
}

bool UProject_JHandoverManager::Tick(float DeltaSeconds)
{
	TickHandover(DeltaSeconds);
	return true;
}

void UProject_JHandoverManager::DispatchTransfer(const FGuid& TransferId)
{
	FProject_JHandoverRecord* Record = HandoverStates.Find(TransferId);
	if (!Record)
	{
		return;
	}
	if (!Transport)
	{
		FailTransfer(*Record, EProject_JHandoverFailureReason::TransportUnavailable);
		return;
	}

	++Record->AttemptCount;
	SetRecordState(*Record, EProject_JHandoverState::Sending);

	FProject_JHandoverTransportRequest Request;
	Request.Envelope = Record->Envelope;
	Request.Attempt = Record->AttemptCount;
	SetRecordState(*Record, EProject_JHandoverState::AwaitingAck);
	const bool bDispatched = Transport->Send(
		Request,
		[WeakThis = TWeakObjectPtr<UProject_JHandoverManager>(this)](
			const FProject_JHandoverTransportResponse& Response)
		{
			if (UProject_JHandoverManager* Manager = WeakThis.Get())
			{
				Manager->HandleTransportResponse(Response);
			}
		});

	if (!bDispatched)
	{
		if (Record->State == EProject_JHandoverState::AwaitingAck)
		{
			ScheduleRetry(*Record, EProject_JHandoverFailureReason::DispatchFailed);
		}
	}
}

void UProject_JHandoverManager::HandleTransportResponse(
	const FProject_JHandoverTransportResponse& Response)
{
	FProject_JHandoverRecord* Record = HandoverStates.Find(Response.TransferId);
	if (!Record ||
		Record->State != EProject_JHandoverState::AwaitingAck ||
		Response.Attempt != Record->AttemptCount)
	{
		return;
	}

	switch (Response.Outcome)
	{
	case EProject_JHandoverTransportOutcome::Accepted:
		CompleteTransfer(*Record);
		break;
	case EProject_JHandoverTransportOutcome::Rejected:
		FailTransfer(*Record, EProject_JHandoverFailureReason::Rejected);
		break;
	case EProject_JHandoverTransportOutcome::TransportError:
	default:
		ScheduleRetry(*Record, EProject_JHandoverFailureReason::DispatchFailed);
		break;
	}
}

void UProject_JHandoverManager::ScheduleRetry(
	FProject_JHandoverRecord& Record,
	EProject_JHandoverFailureReason FailureReason)
{
	if (Record.AttemptCount > FMath::Max(0, MaxRetryCount))
	{
		FailTransfer(Record, FailureReason);
		return;
	}

	SetRecordState(Record, EProject_JHandoverState::Preparing, FailureReason);
	Record.RetryDelayRemaining = FMath::Max(0.0f, RetryDelaySeconds);
	if (Record.RetryDelayRemaining <= 0.0f)
	{
		DispatchTransfer(Record.Envelope.TransferId);
	}
}

void UProject_JHandoverManager::CompleteTransfer(FProject_JHandoverRecord& Record)
{
	SetRecordState(Record, EProject_JHandoverState::Ghosting);
	SetRecordState(Record, EProject_JHandoverState::SwitchingAuthority);
	SetRecordState(Record, EProject_JHandoverState::Completed);
}

void UProject_JHandoverManager::FailTransfer(
	FProject_JHandoverRecord& Record,
	EProject_JHandoverFailureReason FailureReason)
{
	SetRecordState(Record, EProject_JHandoverState::Failed, FailureReason);
}

void UProject_JHandoverManager::SetRecordState(
	FProject_JHandoverRecord& Record,
	EProject_JHandoverState NewState,
	EProject_JHandoverFailureReason FailureReason)
{
	Record.State = NewState;
	Record.FailureReason = FailureReason;
	Record.StateElapsedTime = 0.0f;

	// Release the large payload immediately while retaining diagnostic metadata.
	if (NewState == EProject_JHandoverState::Completed ||
		NewState == EProject_JHandoverState::Failed ||
		NewState == EProject_JHandoverState::Cancelled)
	{
		Record.Envelope.Payload.Empty();
	}

	OnHandoverStateChanged.Broadcast(
		Record.Envelope.TransferId,
		NewState,
		FailureReason);
}

bool UProject_JHandoverManager::HasActiveTransferForActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	for (const auto& Pair : HandoverStates)
	{
		const FProject_JHandoverRecord& Record = Pair.Value;
		if (Record.Actor.Get() == Actor &&
			(Record.State == EProject_JHandoverState::Preparing ||
				Record.State == EProject_JHandoverState::Sending ||
				Record.State == EProject_JHandoverState::AwaitingAck ||
				Record.State == EProject_JHandoverState::Ghosting ||
				Record.State == EProject_JHandoverState::SwitchingAuthority))
		{
			return true;
		}
	}
	return false;
}

void UProject_JHandoverManager::PruneReplayWindow()
{
	const FDateTime ReplayCutoff = FDateTime::UtcNow() - FTimespan::FromSeconds(
		FMath::Max(60.0, static_cast<double>(MaxEnvelopeAgeSeconds)));
	for (auto It = AppliedTransferIds.CreateIterator(); It; ++It)
	{
		if (It.Value() < ReplayCutoff)
		{
			It.RemoveCurrent();
		}
	}
}
