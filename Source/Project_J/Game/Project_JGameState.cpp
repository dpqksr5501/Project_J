// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

double AProject_JGameState::GetServerWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World || NumServerWorldTimeSecondsDeltas == 0) { return Super::GetServerWorldTimeSeconds(); }
	return World->GetTimeSeconds() + PreciseServerWorldTimeSecondsDelta;
}

void AProject_JGameState::OnRep_ReplicatedWorldTimeSecondsDouble()
{
	// The engine remains the sole sample accumulator, including its >250 rebase.
	Super::OnRep_ReplicatedWorldTimeSecondsDouble();
	if (!GetWorld() || NumServerWorldTimeSecondsDeltas == 0) { return; }
	const double TargetMean = SumServerWorldTimeSecondsDelta / double(NumServerWorldTimeSecondsDeltas);
	// Preserve the engine's existing filter response and zero-offset sentinel;
	// only its derived offset retains the double precision of the source samples.
	if (PreciseServerWorldTimeSecondsDelta == 0.0) { PreciseServerWorldTimeSecondsDelta = TargetMean; }
	else { PreciseServerWorldTimeSecondsDelta += (TargetMean - PreciseServerWorldTimeSecondsDelta) * 0.5; }
}

void AProject_JGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AProject_JGameState, WorldInstanceId);
	DOREPLIFETIME(AProject_JGameState, PublicEventId);
}

void AProject_JGameState::SetWorldInstanceId(const FProject_JWorldInstanceId& InWorldInstanceId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (WorldInstanceId.WorldId == InWorldInstanceId.WorldId &&
		WorldInstanceId.ZoneId == InWorldInstanceId.ZoneId &&
		WorldInstanceId.InstanceId == InWorldInstanceId.InstanceId &&
		WorldInstanceId.ChannelId == InWorldInstanceId.ChannelId)
	{
		return;
	}

	WorldInstanceId = InWorldInstanceId;
	ForceNetUpdate();
}

void AProject_JGameState::SetPublicEventId(FName InPublicEventId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (PublicEventId == InPublicEventId)
	{
		return;
	}

	PublicEventId = InPublicEventId;
	ForceNetUpdate();
}
