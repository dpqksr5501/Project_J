#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JSocialSubsystem.generated.h"

class AProject_JPlayerState;

UENUM(BlueprintType)
enum class EProject_JSocialOperationFailure : uint8
{
	None,
	InvalidPlayer,
	InvalidCharacterId,
	AlreadyMember,
	NotMember,
	GroupNotFound,
	GroupFull
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

	/** Trusted server/backend restore path. Creates missing groups when necessary. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Social")
	bool RestoreMembership(AProject_JPlayerState* Player, FName PartyId, FName GuildId);

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FName GetPartyIdForCharacter(const FGuid& CharacterId) const;

	UFUNCTION(BlueprintPure, Category = "MMO|Social")
	FName GetGuildIdForCharacter(const FGuid& CharacterId) const;

private:
	struct FRuntimeGroup
	{
		FName GroupId = NAME_None;
		FGuid LeaderCharacterId;
		TArray<FGuid> Members;
	};

	using FGroupMap = TMap<FName, FRuntimeGroup>;
	using FMembershipMap = TMap<FGuid, FName>;

	static FName CreateGroupId(const TCHAR* Prefix);
	bool ResolveAuthoritativeCharacter(AProject_JPlayerState* Player, FGuid& OutCharacterId) const;
	FProject_JSocialOperationResult CreateGroup(
		AProject_JPlayerState* Leader,
		const TCHAR* Prefix,
		int32 MaxMembers,
		FGroupMap& Groups,
		FMembershipMap& Memberships);
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

	UPROPERTY(Config, EditAnywhere, Category = "MMO|Social", meta = (ClampMin = "2"))
	int32 MaxPartyMembers = 8;

	UPROPERTY(Config, EditAnywhere, Category = "MMO|Social", meta = (ClampMin = "2"))
	int32 MaxGuildMembers = 100;

	TMap<FGuid, TWeakObjectPtr<AProject_JPlayerState>> OnlinePlayers;
	FGroupMap Parties;
	FGroupMap Guilds;
	FMembershipMap CharacterParties;
	FMembershipMap CharacterGuilds;
};
