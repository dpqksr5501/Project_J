#include "Backend/Project_JHandoverManager.h"

#include "GameFramework/Actor.h"
#include "Misc/Crc.h"
#include "Network/Project_JHandoverSerializable.h"
#include "Project_J.h"

void UProject_JHandoverManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Setup connections to adjacent authority nodes if necessary
}

void UProject_JHandoverManager::Deinitialize()
{
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

	const TObjectKey<AActor> ActorKey(ActorToHandover);
	if (const FProject_JHandoverRecord* ExistingRecord = HandoverStates.Find(ActorKey))
	{
		if (ExistingRecord->State == EProject_JHandoverState::Preparing ||
			ExistingRecord->State == EProject_JHandoverState::Ghosting ||
			ExistingRecord->State == EProject_JHandoverState::SwitchingAuthority)
		{
			return;
		}
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

	FProject_JHandoverRecord& Record = HandoverStates.FindOrAdd(ActorKey);
	Record.Actor = ActorToHandover;
	Record.Envelope = MoveTemp(Envelope);
	Record.State = EProject_JHandoverState::Preparing;
	Record.ElapsedTime = 0.0f;

	// In a real implementation:
	// 1. Send payload to TargetServerNodeId to initialize a ghost replica.
	// 2. Move the record to Ghosting once the destination acknowledges.
	// 3. Negotiate Authority Switch via RPC or backend message.
}

bool UProject_JHandoverManager::IsHandoverInProgress(AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	const FProject_JHandoverRecord* Record = HandoverStates.Find(TObjectKey<AActor>(Actor));
	return Record &&
		(Record->State == EProject_JHandoverState::Preparing ||
			Record->State == EProject_JHandoverState::Ghosting ||
			Record->State == EProject_JHandoverState::SwitchingAuthority);
}

void UProject_JHandoverManager::CancelHandover(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	HandoverStates.Remove(TObjectKey<AActor>(Actor));
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
	if (EnvelopeAge.GetTotalSeconds() < -1.0 ||
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

	const EProject_JHandoverValidationFailure ValidationFailure = ValidateEnvelope(Envelope);
	if (ValidationFailure != EProject_JHandoverValidationFailure::None)
	{
		UE_LOG(LogProject_J, Warning, TEXT("Rejected handover envelope for %s: %d"), *GetNameSafe(DestinationActor), static_cast<int32>(ValidationFailure));
		return false;
	}

	const FDateTime ReplayCutoff = FDateTime::UtcNow() - FTimespan::FromSeconds(
		FMath::Max(60.0, static_cast<double>(MaxEnvelopeAgeSeconds)));
	for (auto It = AppliedTransferIds.CreateIterator(); It; ++It)
	{
		if (It.Value() < ReplayCutoff)
		{
			It.RemoveCurrent();
		}
	}
	if (AppliedTransferIds.Contains(Envelope.TransferId))
	{
		UE_LOG(LogProject_J, Warning, TEXT("Rejected replayed handover envelope %s."), *Envelope.TransferId.ToString());
		return false;
	}
	if (Envelope.TargetNodeId != LocalNodeId)
	{
		UE_LOG(LogProject_J, Warning, TEXT("Rejected handover envelope for a different node: %s"), *Envelope.TargetNodeId);
		return false;
	}
	if (Envelope.ActorClassPath != DestinationActor->GetClass()->GetPathName())
	{
		UE_LOG(LogProject_J, Warning, TEXT("Rejected handover envelope due to actor class mismatch for %s."), *GetNameSafe(DestinationActor));
		return false;
	}

	IProject_JHandoverSerializable* SerializableActor = Cast<IProject_JHandoverSerializable>(DestinationActor);
	if (!SerializableActor)
	{
		UE_LOG(LogProject_J, Warning, TEXT("Rejected handover envelope because %s is not serializable."), *GetNameSafe(DestinationActor));
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
