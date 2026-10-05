#include "Social/Project_JSocialSubsystem.h"

#include "Game/Project_JPlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJSocial, Log, All);

void UProject_JSocialSubsystem::BindPlayerState(AProject_JPlayerState* PlayerState)
{
	FGuid CharacterId;
	if (!ResolveAuthoritativeCharacter(PlayerState, CharacterId))
	{
		return;
	}

	OnlinePlayers.Add(CharacterId, PlayerState);
	SynchronizePlayerSnapshot(PlayerState);
}

void UProject_JSocialSubsystem::UnbindPlayerState(AProject_JPlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return;
	}

	const FGuid CharacterId = PlayerState->GetCharacterId().Value;
	if (const TWeakObjectPtr<AProject_JPlayerState>* BoundPlayer = OnlinePlayers.Find(CharacterId);
		BoundPlayer && BoundPlayer->Get() == PlayerState)
	{
		OnlinePlayers.Remove(CharacterId);
	}
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateParty(AProject_JPlayerState* Leader)
{
	return CreateGroup(Leader, TEXT("Party"), MaxPartyMembers, Parties, CharacterParties);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::JoinParty(
	AProject_JPlayerState* Player,
	FName PartyId)
{
	return JoinGroup(Player, PartyId, MaxPartyMembers, Parties, CharacterParties);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::LeaveParty(AProject_JPlayerState* Player)
{
	return LeaveGroup(Player, Parties, CharacterParties);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateGuild(AProject_JPlayerState* Leader)
{
	return CreateGroup(Leader, TEXT("Guild"), MaxGuildMembers, Guilds, CharacterGuilds);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::JoinGuild(
	AProject_JPlayerState* Player,
	FName GuildId)
{
	return JoinGroup(Player, GuildId, MaxGuildMembers, Guilds, CharacterGuilds);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::LeaveGuild(AProject_JPlayerState* Player)
{
	return LeaveGroup(Player, Guilds, CharacterGuilds);
}

bool UProject_JSocialSubsystem::RestoreMembership(
	AProject_JPlayerState* Player,
	FName PartyId,
	FName GuildId)
{
	FGuid CharacterId;
	if (!ResolveAuthoritativeCharacter(Player, CharacterId))
	{
		return false;
	}

	if (!CanRestoreGroupMembership(
			CharacterId,
			PartyId,
			MaxPartyMembers,
			Parties,
			CharacterParties) ||
		!CanRestoreGroupMembership(
			CharacterId,
			GuildId,
			MaxGuildMembers,
			Guilds,
			CharacterGuilds))
	{
		return false;
	}

	const bool bPartyRestored = RestoreGroupMembership(
		CharacterId,
		PartyId,
		MaxPartyMembers,
		Parties,
		CharacterParties);
	const bool bGuildRestored = RestoreGroupMembership(
		CharacterId,
		GuildId,
		MaxGuildMembers,
		Guilds,
		CharacterGuilds);
	if (!bPartyRestored || !bGuildRestored)
	{
		return false;
	}

	OnlinePlayers.Add(CharacterId, Player);
	SynchronizePlayerSnapshot(Player);
	return true;
}

bool UProject_JSocialSubsystem::CanRestoreGroupMembership(
	const FGuid& CharacterId,
	FName GroupId,
	int32 MaxMembers,
	const FGroupMap& Groups,
	const FMembershipMap& Memberships) const
{
	const FName ExistingGroupId = Memberships.FindRef(CharacterId);
	if (GroupId.IsNone())
	{
		const auto* Existing = Groups.Find(ExistingGroupId);
		if (Existing && Existing->Revision == MAX_int64) { return false; }
		return true;
	}
	if (!ExistingGroupId.IsNone() && ExistingGroupId != GroupId)
	{
		return false;
	}
	if (const FRuntimeGroup* Group = Groups.Find(GroupId))
	{
		return !Group->bDeleted && (Group->Members.Contains(CharacterId) ||
			(Group->Revision < MAX_int64 && Group->Members.Num() < FMath::Max(2, MaxMembers)));
	}
	return false;
}

FName UProject_JSocialSubsystem::GetPartyIdForCharacter(const FGuid& CharacterId) const
{
	return CharacterParties.FindRef(CharacterId);
}

FName UProject_JSocialSubsystem::GetGuildIdForCharacter(const FGuid& CharacterId) const
{
	return CharacterGuilds.FindRef(CharacterId);
}

FName UProject_JSocialSubsystem::CreateGroupId(const TCHAR* Prefix)
{
	return FName(*FString::Printf(
		TEXT("%s_%s"),
		Prefix,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
}

bool UProject_JSocialSubsystem::ResolveAuthoritativeCharacter(
	AProject_JPlayerState* Player,
	FGuid& OutCharacterId) const
{
	if (!Player || !Player->HasAuthority())
	{
		return false;
	}

	OutCharacterId = Player->GetCharacterId().Value;
	return OutCharacterId.IsValid();
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateGroup(
	AProject_JPlayerState* Leader,
	const TCHAR* Prefix,
	int32 MaxMembers,
	FGroupMap& Groups,
	FMembershipMap& Memberships)
{
	FGuid CharacterId;
	if (!Leader || !Leader->HasAuthority())
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidPlayer);
	}
	if (!ResolveAuthoritativeCharacter(Leader, CharacterId))
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidCharacterId);
	}
	if (const FName ExistingGroup = Memberships.FindRef(CharacterId); !ExistingGroup.IsNone())
	{
		return FProject_JSocialOperationResult::Failed(
			EProject_JSocialOperationFailure::AlreadyMember,
			ExistingGroup);
	}

	FRuntimeGroup Group;
	Group.GroupId = CreateGroupId(Prefix);
	Group.LeaderCharacterId = CharacterId;
	Group.Members.Add(CharacterId);
	Groups.Add(Group.GroupId, Group);
	Memberships.Add(CharacterId, Group.GroupId);
	OnlinePlayers.Add(CharacterId, Leader);
	SynchronizePlayerSnapshot(Leader);

	UE_LOG(LogProjectJSocial, Log, TEXT("Created %s for character %s."), *Group.GroupId.ToString(), *CharacterId.ToString());
	return FProject_JSocialOperationResult::Success(Group.GroupId);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::JoinGroup(
	AProject_JPlayerState* Player,
	FName GroupId,
	int32 MaxMembers,
	FGroupMap& Groups,
	FMembershipMap& Memberships)
{
	FGuid CharacterId;
	if (!Player || !Player->HasAuthority())
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidPlayer, GroupId);
	}
	if (!ResolveAuthoritativeCharacter(Player, CharacterId))
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidCharacterId, GroupId);
	}
	if (const FName ExistingGroup = Memberships.FindRef(CharacterId); !ExistingGroup.IsNone())
	{
		return FProject_JSocialOperationResult::Failed(
			EProject_JSocialOperationFailure::AlreadyMember,
			ExistingGroup);
	}

	FRuntimeGroup* Group = Groups.Find(GroupId);
	if (!Group || Group->bDeleted)
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::GroupNotFound, GroupId);
	}
	if (Group->Members.Num() >= FMath::Max(2, MaxMembers))
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::GroupFull, GroupId);
	}
	if (Group->Revision == MAX_int64) { return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::RevisionExhausted, GroupId); }

	Group->Members.Add(CharacterId);
	++Group->Revision;
	Memberships.Add(CharacterId, GroupId);
	OnlinePlayers.Add(CharacterId, Player);
	SynchronizePlayerSnapshot(Player);
	return FProject_JSocialOperationResult::Success(GroupId);
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::LeaveGroup(
	AProject_JPlayerState* Player,
	FGroupMap& Groups,
	FMembershipMap& Memberships)
{
	FGuid CharacterId;
	if (!Player || !Player->HasAuthority())
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidPlayer);
	}
	if (!ResolveAuthoritativeCharacter(Player, CharacterId))
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::InvalidCharacterId);
	}

	const FName GroupId = Memberships.FindRef(CharacterId);
	if (GroupId.IsNone())
	{
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::NotMember);
	}

	FRuntimeGroup* Group = Groups.Find(GroupId);
	if (Group && Group->Revision == MAX_int64) { return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::RevisionExhausted, GroupId); }
	Memberships.Remove(CharacterId);
	if (Group)
	{
		Group->Members.Remove(CharacterId);
		++Group->Revision;
		if (Group->Members.IsEmpty())
		{
			Group->bDeleted = true;
			Group->LeaderCharacterId.Invalidate();
		}
		else if (Group->LeaderCharacterId == CharacterId)
		{
			Group->LeaderCharacterId = Group->Members[0];
			SynchronizeGroupMembers(*Group);
		}
	}

	SynchronizePlayerSnapshot(Player);
	return FProject_JSocialOperationResult::Success(GroupId);
}

bool UProject_JSocialSubsystem::RestoreGroupMembership(
	const FGuid& CharacterId,
	FName GroupId,
	int32 MaxMembers,
	FGroupMap& Groups,
	FMembershipMap& Memberships)
{
	const FName ExistingGroupId = Memberships.FindRef(CharacterId);
	if (GroupId.IsNone())
	{
		if (!ExistingGroupId.IsNone())
		{
			if (FRuntimeGroup* ExistingGroup = Groups.Find(ExistingGroupId))
			{
				ExistingGroup->Members.Remove(CharacterId);
				++ExistingGroup->Revision;
				if (ExistingGroup->Members.IsEmpty())
				{
					ExistingGroup->bDeleted = true;
					ExistingGroup->LeaderCharacterId.Invalidate();
				}
				else if (ExistingGroup->LeaderCharacterId == CharacterId)
				{
					ExistingGroup->LeaderCharacterId = ExistingGroup->Members[0];
					SynchronizeGroupMembers(*ExistingGroup);
				}
			}
			Memberships.Remove(CharacterId);
		}
		return true;
	}

	if (!ExistingGroupId.IsNone() && ExistingGroupId != GroupId)
	{
		return false;
	}

	FRuntimeGroup* Existing = Groups.Find(GroupId);
	if (!Existing || Existing->bDeleted) { return false; }
	FRuntimeGroup& Group = *Existing;
	if (!Group.Members.Contains(CharacterId))
	{
		if (Group.Members.Num() >= FMath::Max(2, MaxMembers))
		{
			return false;
		}
		Group.Members.Add(CharacterId);
		++Group.Revision;
	}
	if (!Group.LeaderCharacterId.IsValid())
	{
		Group.LeaderCharacterId = CharacterId;
	}
	Memberships.Add(CharacterId, GroupId);
	return true;
}

void UProject_JSocialSubsystem::SynchronizePlayerSnapshot(AProject_JPlayerState* Player)
{
	if (!Player || !Player->HasAuthority())
	{
		return;
	}

	const FGuid CharacterId = Player->GetCharacterId().Value;
	Player->SetSocialState(
		CharacterParties.FindRef(CharacterId),
		CharacterGuilds.FindRef(CharacterId),
		ResolveGroupLeader(CharacterParties.FindRef(CharacterId), Parties),
		ResolveGroupLeader(CharacterGuilds.FindRef(CharacterId), Guilds));
}

void UProject_JSocialSubsystem::SynchronizeOnlineMember(const FGuid& CharacterId)
{
	if (const TWeakObjectPtr<AProject_JPlayerState>* Player = OnlinePlayers.Find(CharacterId))
	{
		SynchronizePlayerSnapshot(Player->Get());
	}
}

void UProject_JSocialSubsystem::SynchronizeGroupMembers(const FRuntimeGroup& Group)
{
	for (const FGuid& MemberCharacterId : Group.Members)
	{
		SynchronizeOnlineMember(MemberCharacterId);
	}
}

FGuid UProject_JSocialSubsystem::ResolveGroupLeader(FName GroupId, const FGroupMap& Groups)
{
	if (const FRuntimeGroup* Group = Groups.Find(GroupId))
	{
		return Group->LeaderCharacterId;
	}
	return FGuid();
}

bool UProject_JSocialSubsystem::GetGroupSnapshot(EProject_JSocialGroupKind Kind, FName GroupId, FProject_JSocialGroupSnapshot& OutSnapshot) const
{
	const auto& Groups = Kind == EProject_JSocialGroupKind::Party ? Parties : Guilds;
	if (const auto* Group = Groups.Find(GroupId)) { OutSnapshot = *Group; return true; }
	return false;
}

bool UProject_JSocialSubsystem::RestoreGroupSnapshot(EProject_JSocialGroupKind Kind, const FProject_JSocialGroupSnapshot& Snapshot)
{
	check(IsInGameThread());
	auto& Groups = Kind == EProject_JSocialGroupKind::Party ? Parties : Guilds;
	auto& Memberships = Kind == EProject_JSocialGroupKind::Party ? CharacterParties : CharacterGuilds;
	const int32 Capacity = FMath::Max(2, Kind == EProject_JSocialGroupKind::Party ? MaxPartyMembers : MaxGuildMembers);
	if (Snapshot.Version != 1 || Snapshot.GroupId.IsNone() || Snapshot.Revision < 1 || Snapshot.Revision == MAX_int64 || Snapshot.Members.Num() > Capacity) { return false; }
	if (Snapshot.bDeleted ? (!Snapshot.Members.IsEmpty() || Snapshot.LeaderCharacterId.IsValid()) :
		(Snapshot.Members.IsEmpty() || !Snapshot.LeaderCharacterId.IsValid() || !Snapshot.Members.Contains(Snapshot.LeaderCharacterId))) { return false; }
	TSet<FGuid> Members;
	for (const auto& Member : Snapshot.Members)
	{
		const FName ExistingGroup = Memberships.FindRef(Member);
		if (!Member.IsValid() || Members.Contains(Member) || (!ExistingGroup.IsNone() && ExistingGroup != Snapshot.GroupId)) { return false; }
		Members.Add(Member);
	}
	const auto* Existing = Groups.Find(Snapshot.GroupId);
	if (Existing && Snapshot.Revision <= Existing->Revision)
	{
		return Snapshot.Revision == Existing->Revision && Snapshot.bDeleted == Existing->bDeleted &&
			Snapshot.LeaderCharacterId == Existing->LeaderCharacterId && Snapshot.Members == Existing->Members;
	}
	TArray<FGuid> Affected = Snapshot.Members;
	if (Existing)
	{
		for (const auto& Member : Existing->Members) { Memberships.Remove(Member); Affected.AddUnique(Member); }
	}
	// Publish all maps before notifying online PlayerStates (callbacks may reenter).
	Groups.Add(Snapshot.GroupId, Snapshot);
	for (const auto& Member : Snapshot.Members) { Memberships.Add(Member, Snapshot.GroupId); }
	for (const auto& Member : Affected) { SynchronizeOnlineMember(Member); }
	return true;
}
