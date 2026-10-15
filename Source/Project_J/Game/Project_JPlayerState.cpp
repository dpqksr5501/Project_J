// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerState.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UObject/StrongObjectPtr.h"

AProject_JPlayerState::AProject_JPlayerState()
{
	SetNetUpdateFrequency(100.0f);
	QuestComponent = CreateDefaultSubobject<UProject_JQuestComponent>(TEXT("Quests"));
	ProgressionComponent = CreateDefaultSubobject<UProject_JProgressionComponent>(TEXT("Progression"));

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

void AProject_JPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ProgressionComponent->OnChanged.AddUObject(this, &ThisClass::RefreshPublicProgressionSnapshot);
}

void AProject_JPlayerState::OnRep_Owner()
{
	Super::OnRep_Owner();
	// PlayerState/Avatar can replicate before their owning controller resolves.
	if (AbilitySystemComponent) { AbilitySystemComponent->RefreshActorInfoForCurrentActor(this); }
}

void AProject_JPlayerState::RefreshPublicProgressionSnapshot()
{
	const auto& State = ProgressionComponent->GetState();
	if (HasAuthority() && State.Revision > 0)
		SetPublicCharacterSnapshot(State.ClassDefinition ? State.ClassDefinition->ClassId : NAME_None, State.Level);
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
	ProgressionComponent->OnChanged.RemoveAll(this);
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
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}

	if (AccountId.Value == InAccountId.Value && CharacterId.Value == InCharacterId.Value)
	{
		return;
	}

	const TStrongObjectPtr<AProject_JPlayerState> KeepSelf(this);
	const FProject_JAccountId RequestedAccount = InAccountId;
	const FProject_JCharacterId RequestedCharacter = InCharacterId;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (auto* SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>())
		{
			SocialSubsystem->UnbindPlayerState(this);
		}
	}

	AccountId = RequestedAccount;
	CharacterId = RequestedCharacter;
	// The detached identity has no social projection. Membership remains owned
	// by the social service and can be rebound to another live PlayerState.
	if (!RequestedCharacter.IsValid()) SetSocialState(NAME_None, NAME_None, FGuid(), FGuid());
	OnCharacterIdentityChanged.Broadcast();
	// Listeners may retire this player or install a newer identity synchronously.
	if (IsActorBeingDestroyed() || !HasAuthority() || AccountId.Value != RequestedAccount.Value || CharacterId.Value != RequestedCharacter.Value) return;
	ForceNetUpdate();

	if (auto* GameInstance = GetGameInstance())
	{
		if (auto* SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>())
			SocialSubsystem->BindPlayerState(this);
	}
}

void AProject_JPlayerState::OnRep_CharacterId() { OnCharacterIdentityChanged.Broadcast(); }

void AProject_JPlayerState::SetSocialState(
	FName InPartyId,
	FName InGuildId,
	const FGuid& InPartyLeaderCharacterId,
	const FGuid& InGuildLeaderCharacterId)
{
	if (!HasAuthority() || IsActorBeingDestroyed() ||
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
