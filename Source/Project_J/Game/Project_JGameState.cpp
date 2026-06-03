// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameState.h"
#include "Net/UnrealNetwork.h"

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
