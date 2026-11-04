// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerState.h"
#include "Project_JGameMode.h"
#include "Project_JPlayerCharacter.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Companion/Project_JCompanionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Network/Project_JPlayerStateAudienceFilter.h"

AProject_JPlayerState::AProject_JPlayerState()
{
	PartyRoster.OwnerState = this;
	CreateDefaultSubobject<UProject_JCommandBudgetComponent>(TEXT("CommandBudget"));
	CompanionComponent = CreateDefaultSubobject<UProject_JCompanionComponent>(TEXT("Companion"));
	SetNetUpdateFrequency(100.0f);
	QuestComponent = CreateDefaultSubobject<UProject_JQuestComponent>(TEXT("Quests"));
	CraftingJobsComponent = CreateDefaultSubobject<UProject_JCraftingJobsComponent>(TEXT("CraftingJobs"));
	ProgressionComponent = CreateDefaultSubobject<UProject_JProgressionComponent>(TEXT("Progression"));

	InventoryComponent = CreateDefaultSubobject<UProject_JInventoryComponent>(TEXT("InventoryComponent"));
	EquipmentManagerComponent = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("EquipmentManagerComponent"));

	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("AttributeSet"));
}

ELifetimeCondition AProject_JPlayerState::AllowActorComponentToReplicate(const UActorComponent* ComponentToReplicate) const
{
	const ELifetimeCondition SuperCondition = Super::AllowActorComponentToReplicate(ComponentToReplicate);
	if (SuperCondition == COND_Never)
	{
		return SuperCondition;
	}
	// Match native members, not every component of these classes. Foreign clients
	// still construct the defaults (including the equipment public-Pawn facade).
	if (ComponentToReplicate == InventoryComponent || ComponentToReplicate == EquipmentManagerComponent ||
		ComponentToReplicate == QuestComponent.Get() || ComponentToReplicate == CompanionComponent.Get() ||
		ComponentToReplicate == CraftingJobsComponent.Get())
	{
		return COND_OwnerOnly;
	}
	return SuperCondition;
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

void AProject_JPlayerState::OnReplicationStartedForIris(const FOnReplicationStartedParams& Params)
{
	const TStrongObjectPtr<AProject_JPlayerState> KeepSelf(this);
	Super::OnReplicationStartedForIris(Params);
	if (!IsValid(this) || IsActorBeingDestroyed() || !HasAuthority()) return;
	UProject_JPlayerStateAudienceFilter::TryBind(this);
	if (auto* Player = Cast<AProject_JPlayerCharacter>(GetPawn()); IsValid(Player) && Player->GetPlayerState() == this)
	{
		const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
		Player->RefreshPlayerStateCreationDependency(true);
	}
}

void AProject_JPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (auto* Budget = FindComponentByClass<UProject_JCommandBudgetComponent>())
		Budget->CommandAuthority.BindUObject(this, &ThisClass::CanAcceptSessionCommand);
	AbilitySystemComponent->ClientSessionAuthority.BindUObject(this, &ThisClass::CanAcceptSessionCommand);
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
			if (auto* Profiles = GameInstance->GetSubsystem<UProject_JLocalProfileSubsystem>()) Profiles->PlayerBegunPlay(this);
		}
	}
}

void AProject_JPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Authority = ServerSessionAuthority.Get()) Authority->PlayerSessionEnded(this);
	RetirePartyRoster();
	if (auto* GI = GetGameInstance()) if (auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>()) Profiles->Detach(this);
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

void AProject_JPlayerState::RequireServerSession(AProject_JGameMode* Authority)
{
	check(IsInGameThread());
	if (bServerSessionRequired) return;
	bServerSessionRequired = true;
	bServerSessionRevoked = true;
	ServerSessionAuthority = Authority;
}

void AProject_JPlayerState::InstallServerSession(AProject_JGameMode* Authority, const ProjectJ::MMO::FServerSessionLease& Lease)
{
	check(IsInGameThread());
	bServerSessionRequired = true;
	bServerSessionRevoked = false;
	ServerSessionAuthority = Authority;
	ServerSessionLease = Lease;
}

bool AProject_JPlayerState::OwnsServerSession(const AProject_JGameMode* Authority, const ProjectJ::MMO::FServerSessionLease& Lease) const
{
	return bServerSessionRequired && ServerSessionAuthority.Get() == Authority && Lease.IsValid() && ServerSessionLease == Lease;
}

bool AProject_JPlayerState::CanAcceptSessionCommand() const
{
	check(IsInGameThread());
	if (!IsValid(this) || IsActorBeingDestroyed() || !HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown) return false;
	if (!bServerSessionRequired) return true;
	const auto* Authority = ServerSessionAuthority.Get();
	return !bServerSessionRevoked && IsValid(Authority) && !Authority->IsActorBeingDestroyed()
		&& Authority->IsTrustedSessionCurrent(this, ServerSessionLease)
		&& AccountId.Value == ServerSessionLease.AccountId && CharacterId.Value == ServerSessionLease.CharacterId;
}

ProjectJ::MMO::FServerSessionLease AProject_JPlayerState::CaptureServerSession() const
{
	check(IsInGameThread());
	return ServerSessionLease;
}

bool AProject_JPlayerState::IsServerSessionCurrent(const ProjectJ::MMO::FServerSessionLease& Captured) const
{
	check(IsInGameThread());
	return CanAcceptSessionCommand() && ((!bServerSessionRequired && !Captured.IsValid()) || (bServerSessionRequired && Captured == ServerSessionLease));
}

void AProject_JPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AProject_JPlayerState, AccountId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AProject_JPlayerState, CharacterId, COND_OwnerOnly);
	DOREPLIFETIME(AProject_JPlayerState, PublicClassId);
	DOREPLIFETIME(AProject_JPlayerState, PublicCharacterLevel);
	DOREPLIFETIME_CONDITION(AProject_JPlayerState, PartyRoster, COND_OwnerOnly);
	DOREPLIFETIME(AProject_JPlayerState, PartyId);
	DOREPLIFETIME(AProject_JPlayerState, GuildId);
	DOREPLIFETIME(AProject_JPlayerState, PartyLeaderCharacterId);
	DOREPLIFETIME(AProject_JPlayerState, GuildLeaderCharacterId);
}

void AProject_JPlayerState::SetIdentity(const FProject_JAccountId& InAccountId, const FProject_JCharacterId& InCharacterId)
{
	// Explicitly managed states can only publish the trusted server's current
	// account/character pair. Existing prototype/Standalone setters stay intact.
	if (bServerSessionRequired && (bServerSessionRevoked || InAccountId.Value != ServerSessionLease.AccountId || InCharacterId.Value != ServerSessionLease.CharacterId)) return;
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}

	if (AccountId.Value == InAccountId.Value && CharacterId.Value == InCharacterId.Value)
	{
		if (CraftingJobsComponent) CraftingJobsComponent->NotifyOwnerIdentity(CharacterId.Value);
		return;
	}

	const TStrongObjectPtr<AProject_JPlayerState> KeepSelf(this);
	const auto PreviousAccount = AccountId; const auto PreviousCharacter = CharacterId;
	const FProject_JAccountId RequestedAccount = InAccountId;
	const FProject_JCharacterId RequestedCharacter = InCharacterId;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (auto* SocialSubsystem = GameInstance->GetSubsystem<UProject_JSocialSubsystem>())
		{
			SocialSubsystem->UnbindPlayerState(this);
		}
	}

	if (IsActorBeingDestroyed() || !HasAuthority() || AccountId.Value != PreviousAccount.Value || CharacterId.Value != PreviousCharacter.Value) return;
	AccountId = RequestedAccount;
	CharacterId = RequestedCharacter;
	if (CraftingJobsComponent) CraftingJobsComponent->NotifyOwnerIdentity(CharacterId.Value);
	if (IsActorBeingDestroyed() || !HasAuthority() || AccountId.Value != RequestedAccount.Value || CharacterId.Value != RequestedCharacter.Value) return;
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

void AProject_JPlayerState::OnRep_CharacterId()
{
	const TStrongObjectPtr<AProject_JPlayerState> KeepSelf(this);
	const FGuid Expected = CharacterId.Value;
	if (CraftingJobsComponent) CraftingJobsComponent->NotifyOwnerIdentity(Expected);
	if (!IsActorBeingDestroyed() && CharacterId.Value == Expected) OnCharacterIdentityChanged.Broadcast();
}

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
	QueuePartySummary();
}

void AProject_JPlayerState::SetPlayerName(const FString& Name)
{
	const FString Before = GetPlayerName();
	Super::SetPlayerName(Name);
	if (GetPlayerName() != Before) QueuePartySummary();
}
