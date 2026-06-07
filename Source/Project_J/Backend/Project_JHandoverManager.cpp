#include "Backend/Project_JHandoverManager.h"

#include "GameFramework/Actor.h"
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

	IProject_JHandoverSerializable* SerializableActor = Cast<IProject_JHandoverSerializable>(ActorToHandover);
	if (!SerializableActor)
	{
		UE_LOG(LogProject_J, Warning, TEXT("Actor %s does not implement Project_JHandoverSerializable."), *GetNameSafe(ActorToHandover));
		return;
	}

	TArray<uint8> Payload;
	SerializableActor->SerializeForHandover(Payload);

	FProject_JHandoverRecord& Record = HandoverStates.FindOrAdd(ActorKey);
	Record.Actor = ActorToHandover;
	Record.TargetServerNodeId = TargetServerNodeId;
	Record.State = EProject_JHandoverState::Preparing;
	Record.PayloadBytes = Payload.Num();

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
