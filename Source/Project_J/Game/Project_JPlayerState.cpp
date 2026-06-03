// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerState.h"
#include "Net/UnrealNetwork.h"

void AProject_JPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AProject_JPlayerState, AccountId);
	DOREPLIFETIME(AProject_JPlayerState, CharacterId);
	DOREPLIFETIME(AProject_JPlayerState, PublicClassId);
	DOREPLIFETIME(AProject_JPlayerState, PublicCharacterLevel);
}

void AProject_JPlayerState::SetIdentity(const FProject_JAccountId& InAccountId, const FProject_JCharacterId& InCharacterId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AccountId.Value == InAccountId.Value && CharacterId.Value == InCharacterId.Value)
	{
		return;
	}

	AccountId = InAccountId;
	CharacterId = InCharacterId;
	ForceNetUpdate();
}

void AProject_JPlayerState::SetPublicCharacterSnapshot(FName InClassId, int32 InLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 ClampedLevel = FMath::Max(1, InLevel);
	if (PublicClassId == InClassId && PublicCharacterLevel == ClampedLevel)
	{
		return;
	}

	PublicClassId = InClassId;
	PublicCharacterLevel = ClampedLevel;
	ForceNetUpdate();
}
