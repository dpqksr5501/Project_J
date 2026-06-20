// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"

AProject_JPlayerState::AProject_JPlayerState()
{
	SetNetUpdateFrequency(100.0f);

	InventoryComponent = CreateDefaultSubobject<UProject_JInventoryComponent>(TEXT("InventoryComponent"));
	EquipmentManagerComponent = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("EquipmentManagerComponent"));

	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* AProject_JPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UProject_JAbilitySystemComponent* AProject_JPlayerState::GetProjectJAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UProject_JAttributeSet* AProject_JPlayerState::GetProjectJAttributeSet() const
{
	return AttributeSet;
}

void AProject_JPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AProject_JPlayerState, AccountId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AProject_JPlayerState, CharacterId, COND_OwnerOnly);
	DOREPLIFETIME(AProject_JPlayerState, PublicClassId);
	DOREPLIFETIME(AProject_JPlayerState, PublicCharacterLevel);
	DOREPLIFETIME(AProject_JPlayerState, PartyId);
	DOREPLIFETIME(AProject_JPlayerState, GuildId);
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

void AProject_JPlayerState::SetSocialState(FName InPartyId, FName InGuildId)
{
	if (!HasAuthority() || (PartyId == InPartyId && GuildId == InGuildId))
	{
		return;
	}

	PartyId = InPartyId;
	GuildId = InGuildId;
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
