#include "Social/Project_JSocialSubsystem.h"

#include "Game/Project_JPlayerState.h"
#include "Project_JAttributeSet.h"
#include "UObject/StrongObjectPtr.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJSocial, Log, All);

void UProject_JSocialSubsystem::BindPlayerState(AProject_JPlayerState* PlayerState)
{
	FGuid CharacterId;
	if (ResolveAuthoritativeCharacter(PlayerState, CharacterId, false) != EProject_JSocialOperationFailure::None)
	{
		return;
	}

	const auto Previous = OnlinePlayers.FindRef(CharacterId);
	if (Previous.IsValid() && Previous.Get() != PlayerState)
	{
		Previous->bSocialSessionRevoked = true;
		Previous->SetSocialState(NAME_None, NAME_None, FGuid(), FGuid());
	}
	PlayerState->bSocialSessionRevoked = false;
	OnlinePlayers.Add(CharacterId, PlayerState);
	SynchronizePlayerSnapshot(PlayerState);
	if (Previous.IsValid() && Previous.Get() != PlayerState && Previous->bSocialSessionRevoked)
	{
		Previous->StopPartySummaryObservation();
		Previous->ApplyPartyRoster({});
	}
	ReconcilePartyRoster(CharacterParties.FindRef(CharacterId));
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
		const FName PreviousParty = CharacterParties.FindRef(CharacterId);
		OnlinePlayers.Remove(CharacterId);
		PlayerState->bSocialSessionRevoked = true;
		PlayerState->StopPartySummaryObservation();
		PlayerState->SetSocialState(NAME_None, NAME_None, FGuid(), FGuid());
		PlayerState->ApplyPartyRoster({});
		ReconcilePartyRoster(PreviousParty);
	}
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateParty(AProject_JPlayerState* Leader)
{
	const auto Result = CreateGroup(Leader, TEXT("Party"), FMath::Clamp(MaxPartyMembers, 2, ProjectJMaxPartyRosterMembers), Parties, CharacterParties, PartyIds);
	if (Result.bSucceeded) ReconcilePartyRoster(Result.GroupId);
	return Result;
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::JoinParty(
	AProject_JPlayerState* Player,
	FName PartyId)
{
	const auto Result = JoinGroup(Player, PartyId, FMath::Clamp(MaxPartyMembers, 2, ProjectJMaxPartyRosterMembers), Parties, CharacterParties);
	if (Result.bSucceeded) ReconcilePartyRoster(Result.GroupId);
	return Result;
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::LeaveParty(AProject_JPlayerState* Player)
{
	const auto Result = LeaveGroup(Player, Parties, CharacterParties);
	if (Result.bSucceeded)
	{
		Player->StopPartySummaryObservation();
		Player->ApplyPartyRoster({});
		ReconcilePartyRoster(Result.GroupId);
	}
	return Result;
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateGuild(AProject_JPlayerState* Leader)
{
	return CreateGroup(Leader, TEXT("Guild"), MaxGuildMembers, Guilds, CharacterGuilds, GuildIds);
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
	if (ResolveAuthoritativeCharacter(Player, CharacterId) != EProject_JSocialOperationFailure::None)
	{
		return false;
	}

	const FName PreviousParty = CharacterParties.FindRef(CharacterId);
	if (!CanRestoreGroupMembership(
			CharacterId,
			PartyId,
			FMath::Clamp(MaxPartyMembers, 2, ProjectJMaxPartyRosterMembers),
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
		FMath::Clamp(MaxPartyMembers, 2, ProjectJMaxPartyRosterMembers),
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
	if (PartyId.IsNone())
	{
		Player->StopPartySummaryObservation();
		Player->ApplyPartyRoster({});
	}
	ReconcilePartyRoster(PreviousParty);
	if (PartyId != PreviousParty) ReconcilePartyRoster(PartyId);
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

FName UProject_JSocialSubsystem::CreateGroupId(const TCHAR* Prefix, FGroupIdSequence& Sequence)
{
	// FName stores 0 as no number and accepts positive internal int32 numbers.
	// Do not wrap: an old group ID must never identify a newly created group.
	if (Sequence.LastIssued == MAX_int32) return NAME_None;
	if (Sequence.Namespace.IsNone())
		Sequence.Namespace = FName(*FString::Printf(TEXT("%s_%s"), Prefix,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	return FName(Sequence.Namespace, ++Sequence.LastIssued);
}

EProject_JSocialOperationFailure UProject_JSocialSubsystem::ResolveAuthoritativeCharacter(
	AProject_JPlayerState* Player,
	FGuid& OutCharacterId, bool bCheckSession) const
{
	if (bEnding || !IsValid(Player) || Player->IsActorBeingDestroyed() || Player->bPartyRosterEnding || !Player->HasAuthority())
	{
		return EProject_JSocialOperationFailure::InvalidPlayer;
	}

	OutCharacterId = Player->GetCharacterId().Value;
	if (bCheckSession && (Player->bSocialSessionRevoked ||
		(OnlinePlayers.FindRef(OutCharacterId).IsValid() && OnlinePlayers.FindRef(OutCharacterId).Get() != Player)))
		return EProject_JSocialOperationFailure::StaleSession;
	return OutCharacterId.IsValid() ? EProject_JSocialOperationFailure::None : EProject_JSocialOperationFailure::InvalidCharacterId;
}

FProject_JSocialOperationResult UProject_JSocialSubsystem::CreateGroup(
	AProject_JPlayerState* Leader,
	const TCHAR* Prefix,
	int32 MaxMembers,
	FGroupMap& Groups,
	FMembershipMap& Memberships,
	FGroupIdSequence& Sequence)
{
	FGuid CharacterId;
	if (const auto Failure = ResolveAuthoritativeCharacter(Leader, CharacterId); Failure != EProject_JSocialOperationFailure::None)
	{
		return FProject_JSocialOperationResult::Failed(Failure);
	}
	if (const FName ExistingGroup = Memberships.FindRef(CharacterId); !ExistingGroup.IsNone())
	{
		return FProject_JSocialOperationResult::Failed(
			EProject_JSocialOperationFailure::AlreadyMember,
			ExistingGroup);
	}

	// Count live and deleted identities together. Reject before issuing a name
	// or publishing any membership; deletion history is not a reusable slot.
	if (Groups.Num() >= FMath::Clamp(MaxTrackedGroupsPerKind, 0, 1048576))
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::HistoryCapacity);

	FRuntimeGroup Group;
	Group.GroupId = CreateGroupId(Prefix, Sequence);
	if (Group.GroupId.IsNone())
		return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::RevisionExhausted);
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
	if (const auto Failure = ResolveAuthoritativeCharacter(Player, CharacterId); Failure != EProject_JSocialOperationFailure::None)
	{
		return FProject_JSocialOperationResult::Failed(Failure, GroupId);
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
	if (const auto Failure = ResolveAuthoritativeCharacter(Player, CharacterId); Failure != EProject_JSocialOperationFailure::None)
	{
		return FProject_JSocialOperationResult::Failed(Failure);
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
	FGuid CharacterId;
	if (ResolveAuthoritativeCharacter(Player, CharacterId) != EProject_JSocialOperationFailure::None)
	{
		return;
	}

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
	OutSnapshot = {};
	if (Kind != EProject_JSocialGroupKind::Party && Kind != EProject_JSocialGroupKind::Guild) return false;
	const auto& Groups = Kind == EProject_JSocialGroupKind::Party ? Parties : Guilds;
	if (const auto* Group = Groups.Find(GroupId)) { OutSnapshot = *Group; return true; }
	return false;
}

bool UProject_JSocialSubsystem::RestoreGroupSnapshot(EProject_JSocialGroupKind Kind, const FProject_JSocialGroupSnapshot& Snapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_SocialRestoreSnapshot);
	check(IsInGameThread());
	if (bEnding || (Kind != EProject_JSocialGroupKind::Party && Kind != EProject_JSocialGroupKind::Guild)) return false;
	auto& Groups = Kind == EProject_JSocialGroupKind::Party ? Parties : Guilds;
	auto& Memberships = Kind == EProject_JSocialGroupKind::Party ? CharacterParties : CharacterGuilds;
	const int32 Capacity = Kind == EProject_JSocialGroupKind::Party ? FMath::Clamp(MaxPartyMembers, 2, ProjectJMaxPartyRosterMembers) : FMath::Max(2, MaxGuildMembers);
	if (Snapshot.Version != 1 || Snapshot.GroupId.IsNone() || Snapshot.Revision < 1 || Snapshot.Revision == MAX_int64 || Snapshot.Members.Num() > Capacity) { return false; }
	if (Snapshot.bDeleted ? (!Snapshot.Members.IsEmpty() || Snapshot.LeaderCharacterId.IsValid()) :
		(Snapshot.Members.IsEmpty() || !Snapshot.LeaderCharacterId.IsValid() || !Snapshot.Members.Contains(Snapshot.LeaderCharacterId))) { return false; }
	TSet<FGuid> Members;
	Members.Reserve(Snapshot.Members.Num());
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
	// Existing IDs keep their revision/restore contract even after a policy
	// shrink. A new ID cannot alter membership or reserve a future ordinal at capacity.
	if (!Existing && Groups.Num() >= FMath::Clamp(MaxTrackedGroupsPerKind, 0, 1048576)) return false;
	// Snapshot order still owns future leader succession. Membership and online
	// projections only need the set/leader delta, not a full remove/reinsert pass.
	TArray<FGuid> Affected;
	Affected.Reserve(Snapshot.Members.Num());
	const bool bLeaderChanged = !Existing || Existing->LeaderCharacterId != Snapshot.LeaderCharacterId;
	if (Existing)
	{
		for (const auto& Member : Existing->Members)
		{
			if (!Members.Contains(Member))
			{
				Memberships.Remove(Member);
				Affected.Add(Member);
			}
		}
	}
	// A trusted restore can contain a future number from this live namespace.
	// Reserve its high-water mark before publication so later local creation
	// cannot overwrite that group or reuse its ID, including after deletion.
	auto& Sequence = Kind == EProject_JSocialGroupKind::Party ? PartyIds : GuildIds;
	if (!Sequence.Namespace.IsNone() && Snapshot.GroupId.GetComparisonIndex() == Sequence.Namespace.GetComparisonIndex())
		Sequence.LastIssued = FMath::Max(Sequence.LastIssued, Snapshot.GroupId.GetNumber());
	// Publish all maps before notifying online PlayerStates (callbacks may reenter).
	Groups.Add(Snapshot.GroupId, Snapshot);
	for (const auto& Member : Snapshot.Members)
	{
		const bool bJoined = Memberships.FindRef(Member) != Snapshot.GroupId;
		if (bJoined) { Memberships.Add(Member, Snapshot.GroupId); }
		// Removed and current members are disjoint; validated membership is unique.
		if (bJoined || bLeaderChanged) { Affected.Add(Member); }
	}
	for (const auto& Member : Affected) { SynchronizeOnlineMember(Member); }
	if (Kind == EProject_JSocialGroupKind::Party)
	{
		for (const auto& Member : Affected)
			if (CharacterParties.FindRef(Member).IsNone())
				if (auto* Player = ResolveOnlineMember(Member))
				{
					Player->StopPartySummaryObservation(); Player->ApplyPartyRoster({});
				}
		ReconcilePartyRoster(Snapshot.GroupId);
	}
	return true;
}


void UProject_JSocialSubsystem::Deinitialize()
{
    bEnding = true;
    for (const auto& Entry : OnlinePlayers)
        if (auto* Player = Entry.Value.Get()) Player->StopPartySummaryObservation();
    OnlinePlayers.Reset();
    Super::Deinitialize();
}

AProject_JPlayerState* UProject_JSocialSubsystem::ResolveOnlineMember(const FGuid& CharacterId) const
{
    auto* Player = OnlinePlayers.FindRef(CharacterId).Get();
    FGuid Current;
    return ResolveAuthoritativeCharacter(Player, Current) == EProject_JSocialOperationFailure::None
        && Current == CharacterId ? Player : nullptr;
}

FProject_JPartyMemberSummary UProject_JSocialSubsystem::BuildPartyMemberSummary(const FRuntimeGroup& Group, int32 Order) const
{
    FProject_JPartyMemberSummary Result;
    Result.CharacterId = Group.Members[Order]; Result.PartyId = Group.GroupId;
    Result.Order = static_cast<uint8>(Order); Result.bLeader = Result.CharacterId == Group.LeaderCharacterId;
    if (auto* Player = ResolveOnlineMember(Result.CharacterId))
    {
        Result.bAvailableHere = true;
        Result.DisplayName = Player->GetPlayerName().Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\t"), TEXT(" ")).Left(64);
        Result.Level = FMath::Max(1, Player->GetPublicCharacterLevel());
        if (const auto* Attributes = Player->GetProjectJAttributeSet(); IsValid(Attributes))
        {
            const double Health = Attributes->GetHealth(), Maximum = Attributes->GetMaxHealth();
            Result.bHealthKnown = FMath::IsFinite(Health) && FMath::IsFinite(Maximum) && Maximum > 0;
            if (Result.bHealthKnown) Result.HealthPermille = FMath::RoundToInt(FMath::Clamp(Health / Maximum, 0.0, 1.0) * 1000.0);
        }
    }
    return Result;
}

void UProject_JSocialSubsystem::ReconcilePartyRoster(FName PartyId)
{
    if (bEnding || PartyId.IsNone()) return;
    const auto* Found = Parties.Find(PartyId);
    if (!Found || Found->bDeleted || Found->Members.Num() > ProjectJMaxPartyRosterMembers) return;
    // Never retain a map/array reference while owner UI callbacks can reenter.
    const FRuntimeGroup Group = *Found;
    TArray<TStrongObjectPtr<AProject_JPlayerState>> Recipients;
    Recipients.Reserve(Group.Members.Num());
    for (int32 Index = 0; Index < Group.Members.Num(); ++Index)
    {
        if (auto* Player = ResolveOnlineMember(Group.Members[Index])) Recipients.Emplace(Player);
    }
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    for (const auto& Recipient : Recipients)
    {
        const auto* Current = Parties.Find(PartyId);
        if (bEnding || !Current || Current->Revision != Group.Revision || Current->bDeleted) return;
        const FGuid Character = Recipient->GetCharacterId().Value;
        if (CharacterParties.FindRef(Character) != PartyId || ResolveOnlineMember(Character) != Recipient.Get()) continue;
        // A preceding recipient callback may unbind/replace another member
        // without advancing group revision. Rebuild only this bounded8 view.
        TArray<FProject_JPartyMemberSummary> Rows;
        Rows.Reserve(Group.Members.Num());
        for (int32 Index = 0; Index < Group.Members.Num(); ++Index) Rows.Add(BuildPartyMemberSummary(Group, Index));
        Recipient->SetPartySummaryObservation(this, true);
        Recipient->ApplyPartyRoster(Rows);
    }
}

void UProject_JSocialSubsystem::PublishPartyMemberSummary(AProject_JPlayerState* Source)
{
    FGuid Character;
    if (ResolveAuthoritativeCharacter(Source, Character) != EProject_JSocialOperationFailure::None || ResolveOnlineMember(Character) != Source) return;
    const FName PartyId = CharacterParties.FindRef(Character);
    const auto* Found = Parties.Find(PartyId);
    if (!Found || Found->bDeleted || Found->Members.Num() > ProjectJMaxPartyRosterMembers) return;
    const FRuntimeGroup Group = *Found;
    const int32 Order = Group.Members.Find(Character);
    if (Order == INDEX_NONE) return;
    TArray<TStrongObjectPtr<AProject_JPlayerState>> Recipients;
    for (const auto& Member : Group.Members)
        if (auto* Player = ResolveOnlineMember(Member)) Recipients.Emplace(Player);
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JPlayerState> KeepSource(Source);
    for (const auto& Recipient : Recipients)
    {
        const auto* Current = Parties.Find(PartyId);
        if (bEnding || !Current || Current->bDeleted || Current->Revision != Group.Revision
            || ResolveOnlineMember(Character) != Source || CharacterParties.FindRef(Character) != PartyId) return;
        const FGuid Target = Recipient->GetCharacterId().Value;
        if (ResolveOnlineMember(Target) != Recipient.Get() || CharacterParties.FindRef(Target) != PartyId) continue;
        auto Rows = Recipient->GetPartyRoster();
        auto* Existing = Rows.FindByPredicate([&](const auto& Item) { return Item.CharacterId == Character; });
        if (!Existing || Rows.Num() != Group.Members.Num()) { ReconcilePartyRoster(PartyId); return; }
        // Source health/name may also change during a previous local callback.
        // Keep the frequent path to one source read/one dirty row per recipient.
        *Existing = BuildPartyMemberSummary(Group, Order);
        Recipient->ApplyPartyRoster(Rows);
    }
}


bool UProject_JSocialSubsystem::CapturePartyAuthority(AProject_JPlayerState* Requester, FProject_JPartyAuthorityFence& Out) const
{
    check(IsInGameThread());
    Out = {};
    FGuid Character;
    if (ResolveAuthoritativeCharacter(Requester, Character) != EProject_JSocialOperationFailure::None
        || !Requester->CanAcceptSessionCommand() || OnlinePlayers.FindRef(Character).Get() != Requester) return false;
    const FName Party = CharacterParties.FindRef(Character);
    const auto* Group = Parties.Find(Party);
    if (!Group || Group->bDeleted || Group->LeaderCharacterId != Character || !Group->Members.Contains(Character)) return false;
    Out.Requester = Requester; Out.Session = Requester->CaptureServerSession();
    Out.CharacterId = Character; Out.PartyId = Party; Out.Revision = Group->Revision;
    return true;
}
bool UProject_JSocialSubsystem::IsPartyAuthorityCurrent(const FProject_JPartyAuthorityFence& Fence) const
{
    check(IsInGameThread());
    auto* Requester = Fence.Requester.Get();
    FGuid Character;
    if (ResolveAuthoritativeCharacter(Requester, Character) != EProject_JSocialOperationFailure::None
        || Character != Fence.CharacterId || !Requester->IsServerSessionCurrent(Fence.Session)
        || OnlinePlayers.FindRef(Character).Get() != Requester || Fence.PartyId.IsNone()
        || CharacterParties.FindRef(Character) != Fence.PartyId) return false;
    const auto* Group = Parties.Find(Fence.PartyId);
    return Group && !Group->bDeleted && Group->Revision == Fence.Revision
        && Group->LeaderCharacterId == Character && Group->Members.Contains(Character);
}
FProject_JSocialOperationResult UProject_JSocialSubsystem::TransferPartyLeadership(const FProject_JPartyAuthorityFence& Fence, FGuid NewLeader)
{
    if (!IsPartyAuthorityCurrent(Fence)) return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::StaleAuthority, Fence.PartyId);
    auto& Group = Parties.FindChecked(Fence.PartyId);
    if (!NewLeader.IsValid() || !Group.Members.Contains(NewLeader) || CharacterParties.FindRef(NewLeader) != Fence.PartyId)
        return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::NotMember, Fence.PartyId);
    if (NewLeader == Group.LeaderCharacterId) return FProject_JSocialOperationResult::Success(Fence.PartyId);
    if (Group.Revision == MAX_int64) return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::RevisionExhausted, Fence.PartyId);
    // No extensible calls between the fence and mutation. Copy before publication callbacks.
    Group.LeaderCharacterId = NewLeader; ++Group.Revision;
    const auto Published = Group;
    SynchronizeGroupMembers(Published); ReconcilePartyRoster(Fence.PartyId);
    return FProject_JSocialOperationResult::Success(Fence.PartyId);
}
FProject_JSocialOperationResult UProject_JSocialSubsystem::RemovePartyMember(const FProject_JPartyAuthorityFence& Fence, FGuid Member)
{
    if (!IsPartyAuthorityCurrent(Fence)) return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::StaleAuthority, Fence.PartyId);
    auto& Group = Parties.FindChecked(Fence.PartyId);
    if (!Member.IsValid() || Member == Group.LeaderCharacterId || !Group.Members.Contains(Member) || CharacterParties.FindRef(Member) != Fence.PartyId)
        return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::NotMember, Fence.PartyId);
    if (Group.Revision == MAX_int64) return FProject_JSocialOperationResult::Failed(EProject_JSocialOperationFailure::RevisionExhausted, Fence.PartyId);
    Group.Members.Remove(Member); CharacterParties.Remove(Member); ++Group.Revision;
    const auto Published = Group;
    SynchronizeOnlineMember(Member);
    if (auto* Removed = ResolveOnlineMember(Member); Removed && CharacterParties.FindRef(Member).IsNone())
    { Removed->StopPartySummaryObservation(); Removed->ApplyPartyRoster({}); }
    SynchronizeGroupMembers(Published); ReconcilePartyRoster(Fence.PartyId);
    return FProject_JSocialOperationResult::Success(Fence.PartyId);
}
