#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MMO/Coordination.h"
#include "Project_JSocialSubsystem.generated.h"

class AProject_JPlayerState;
struct FProject_JPartyMemberSummary;

/** Server-captured execution fence, never accepted as client authorization.
 * Consumers must revalidate immediately before mutation, without callbacks between.
 */
struct FProject_JPartyAuthorityFence
{
    TWeakObjectPtr<AProject_JPlayerState> Requester;
    ProjectJ::MMO::FServerSessionLease Session;
    FGuid CharacterId;
    FName PartyId;
    int64 Revision = 0;
};

UENUM(BlueprintType)
enum class EProject_JSocialGroupKind : uint8 { Party, Guild };

USTRUCT(BlueprintType)
struct FProject_JSocialGroupSnapshot
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Version = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName GroupId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid LeaderCharacterId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FGuid> Members;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int64 Revision = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bDeleted = false;
};

UENUM(BlueprintType)
enum class EProject_JSocialOperationFailure : uint8
{
	None,
	InvalidPlayer,
	InvalidCharacterId,
	AlreadyMember,
	NotMember,
	GroupNotFound,
	GroupFull,
	RevisionExhausted,
	StaleSession,
	HistoryCapacity,
	StaleAuthority,
	NotLeader
};

USTRUCT(BlueprintType)
struct FProject_JSocialOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	EProject_JSocialOperationFailure Failure = EProject_JSocialOperationFailure::InvalidPlayer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Social")
	FName GroupId = NAME_None;

	static FProject_JSocialOperationResult Success(FName InGroupId)
	{
		FProject_JSocialOperationResult Result;
		Result.bSucceeded = true;
		Result.Failure = EProject_JSocialOperationFailure::None;
		Result.GroupId = InGroupId;
		return Result;
	}

	static FProject_JSocialOperationResult Failed(
		EProject_JSocialOperationFailure FailureReason,
		FName InGroupId = NAME_None)
	{
		FProject_JSocialOperationResult Result;
		Result.Failure = FailureReason;
		Result.GroupId = InGroupId;
		return Result;
	}
};

/**
 * Server-authoritative in-memory social membership service.
 *
 * The subsystem owns party/guild membership. PlayerState only carries the
 * replicated snapshot consumed by UI and replication relevance policies.
 * A persistent backend can later restore memberships through RestoreMembership.
 */
UCLASS(Config=Game, DefaultConfig)
class PROJECT_J_API UProject_JSocialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
    bool CapturePartyAuthority(AProject_JPlayerState* Requester, FProject_JPartyAuthorityFence& Out) const;
    bool IsPartyAuthorityCurrent(const FProject_JPartyAuthorityFence& Fence) const;
    FProject_JSocialOperationResult TransferPartyLeadership(const FProject_JPartyAuthorityFence& Fence, FGuid NewLeader);
    FProject_JSocialOperationResult RemovePartyMember(const FProject_JPartyAuthorityFence& Fence, FGuid Member);
	virtual void Deinitialize() override;
	void BindPlayerState(AProject_JPlayerState* PlayerState);
	void UnbindPlayerState(AProject_JPlayerState* PlayerState);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Party")
	FProject_JSocialOperationResult CreateParty(AProject_JPlayerState* Leader);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Party")
	FProject_JSocialOperationResult JoinParty(AProject_JPlayerState* Player, FName PartyId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Party")
	FProject_JSocialOperationResult LeaveParty(AProject_JPlayerState* Player);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Guild")
	FProject_JSocialOperationResult CreateGuild(AProject_JPlayerState* Leader);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Guild")
	FProject_JSocialOperationResult JoinGuild(AProject_JPlayerState* Player, FName GuildId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social|Guild")
	FProject_JSocialOperationResult LeaveGuild(AProject_JPlayerState* Player);

	/** Compatibility membership path. Restore the complete group first for an unknown ID. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social")
	bool RestoreMembership(AProject_JPlayerState* Player, FName PartyId, FName GuildId);

	/** Trusted backend projection, including explicit leader, revision and deletion tombstone. */
	bool RestoreGroupSnapshot(EProject_JSocialGroupKind Kind, const FProject_JSocialGroupSnapshot& Snapshot);
	bool GetGroupSnapshot(EProject_JSocialGroupKind Kind, FName GroupId, FProject_JSocialGroupSnapshot& OutSnapshot) const;

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FName GetPartyIdForCharacter(const FGuid& CharacterId) const;

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FName GetGuildIdForCharacter(const FGuid& CharacterId) const;

private:
	friend class AProject_JPlayerState;
	friend class FProjectJSocialGroupNameTest;
	void PublishPartyMemberSummary(AProject_JPlayerState* Source);
	void ReconcilePartyRoster(FName PartyId);
	AProject_JPlayerState* ResolveOnlineMember(const FGuid& CharacterId) const;
	bool bEnding = false;
	using FRuntimeGroup = FProject_JSocialGroupSnapshot;
	FProject_JPartyMemberSummary BuildPartyMemberSummary(const FRuntimeGroup& Group, int32 Order) const;

	using FGroupMap = TMap<FName, FRuntimeGroup>;
	using FMembershipMap = TMap<FGuid, FName>;

	// One unique namespace per GI/kind; internal FName numbers never wrap or repeat.
	struct FGroupIdSequence { FName Namespace; int32 LastIssued = 0; };
	FGroupIdSequence PartyIds, GuildIds;
	static FName CreateGroupId(const TCHAR* Prefix, FGroupIdSequence& Sequence);
	EProject_JSocialOperationFailure ResolveAuthoritativeCharacter(AProject_JPlayerState* Player, FGuid& OutCharacterId, bool bCheckSession = true) const;
	FProject_JSocialOperationResult CreateGroup(
		AProject_JPlayerState* Leader,
		const TCHAR* Prefix,
		int32 MaxMembers,
		FGroupMap& Groups,
		FMembershipMap& Memberships,
		FGroupIdSequence& Sequence);
	FProject_JSocialOperationResult JoinGroup(
		AProject_JPlayerState* Player,
		FName GroupId,
		int32 MaxMembers,
		FGroupMap& Groups,
		FMembershipMap& Memberships);
	FProject_JSocialOperationResult LeaveGroup(
		AProject_JPlayerState* Player,
		FGroupMap& Groups,
		FMembershipMap& Memberships);
	bool RestoreGroupMembership(
		const FGuid& CharacterId,
		FName GroupId,
		int32 MaxMembers,
		FGroupMap& Groups,
		FMembershipMap& Memberships);
	bool CanRestoreGroupMembership(
		const FGuid& CharacterId,
		FName GroupId,
		int32 MaxMembers,
		const FGroupMap& Groups,
		const FMembershipMap& Memberships) const;
	void SynchronizePlayerSnapshot(AProject_JPlayerState* Player);
	void SynchronizeOnlineMember(const FGuid& CharacterId);
	void SynchronizeGroupMembers(const FRuntimeGroup& Group);
	static FGuid ResolveGroupLeader(FName GroupId, const FGroupMap& Groups);

	UPROPERTY(Config, EditAnywhere, Category = "MMO|Social", meta = (ClampMin = "2", ClampMax = "8"))
	int32 MaxPartyMembers = 8;

	UPROPERTY(Config, EditAnywhere, Category = "MMO|Social", meta = (ClampMin = "2"))
	int32 MaxGuildMembers = 100;

	/** Provisional in-memory entry ceiling per kind, including deletion history.
	 *  Zero closes new IDs; existing identities and their replay fences are retained. */
	UPROPERTY(Config, EditAnywhere, Category = "MMO|Social", meta = (ClampMin = "0", ClampMax = "1048576"))
	int32 MaxTrackedGroupsPerKind = 65536;

	TMap<FGuid, TWeakObjectPtr<AProject_JPlayerState>> OnlinePlayers;
	FGroupMap Parties;
	FGroupMap Guilds;
	FMembershipMap CharacterParties;
	FMembershipMap CharacterGuilds;
};
