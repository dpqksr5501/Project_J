// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Social/Project_JSocialSubsystem.h"

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

void AProject_JPlayerState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProject_JSocialSubsystem* SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>())
			{
				SocialSubsystem->BindPlayerState(this);
			}
		}
	}
}

void AProject_JPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UProject_JSocialSubsystem* SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>())
			{
				SocialSubsystem->UnbindPlayerState(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
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
	DOREPLIFETIME(AProject_JPlayerState, PartyLeaderCharacterId);
	DOREPLIFETIME(AProject_JPlayerState, GuildLeaderCharacterId);
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

	UProject_JSocialSubsystem* SocialSubsystem = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>();
		if (SocialSubsystem)
		{
			SocialSubsystem->UnbindPlayerState(this);
		}
	}

	AccountId = InAccountId;
	CharacterId = InCharacterId;
	ForceNetUpdate();

	if (SocialSubsystem)
	{
		SocialSubsystem->BindPlayerState(this);
	}
}

void AProject_JPlayerState::SetSocialState(
	FName InPartyId,
	FName InGuildId,
	const FGuid& InPartyLeaderCharacterId,
	const FGuid& InGuildLeaderCharacterId)
{
	if (!HasAuthority() ||
		(PartyId == InPartyId &&
			GuildId == InGuildId &&
			PartyLeaderCharacterId == InPartyLeaderCharacterId &&
			GuildLeaderCharacterId == InGuildLeaderCharacterId))
	{
		return;
	}

	PartyId = InPartyId;
	GuildId = InGuildId;
	PartyLeaderCharacterId = InPartyLeaderCharacterId;
	GuildLeaderCharacterId = InGuildLeaderCharacterId;
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
