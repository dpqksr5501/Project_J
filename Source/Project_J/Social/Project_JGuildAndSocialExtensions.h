#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JGuildAndSocialExtensions.generated.h"

class UProject_JEconomySubsystem;

UENUM(BlueprintType)
enum class EProject_JGuildRank : uint8
{
	Initiate = 0,
	Member = 1,
	Officer = 2,
	Master = 3
};

UENUM(BlueprintType)
enum class EProject_JChatChannel : uint8
{
	Local,
	Party,
	Guild,
	Whisper,
	World
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JGuildBankSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guild|Bank")
	int32 SlotIndex = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guild|Bank")
	FGuid ItemInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guild|Bank")
	FName DefinitionId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guild|Bank")
	int64 Revision = 1;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JPresenceInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Social|Presence")
	FGuid CharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Social|Presence")
	bool bOnline = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Social|Presence")
	FName ZoneId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Social|Presence")
	int32 ChannelId = 0;
};

/**
 * Persistent Social Extensions complementing UProject_JSocialSubsystem (BP01):
 *  - Guild Bank with RBAC ranks, daily withdrawal limits, per-slot revision optimistic locking, audit logs
 *  - Friends & Global Block list (blocks chat, whispers, party/trade invites)
 *  - Chat channel rate-limiting (World 10s, others 1s) & Presence projection
 */
UCLASS()
class PROJECT_J_API UProject_JGuildAndSocialExtensionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// --- Guild Bank RBAC & Optimistic Locking ---
	void SetGuildMemberRank(const FGuid& GuildId, const FGuid& CharacterId, EProject_JGuildRank Rank);
	bool DepositToGuildBank(const FGuid& GuildId, const FGuid& ActorCharId, int32 SlotIndex, const FGuid& ItemInstanceId, FName DefinitionId);
	bool WithdrawFromGuildBank(const FGuid& GuildId, const FGuid& ActorCharId, int32 SlotIndex, int64 ExpectedRevision, int32 DayKey, FGuid& OutItemInstanceId);
	bool WithdrawFromGuildBankToMail(const FGuid& GuildId, const FGuid& ActorCharId, int32 SlotIndex, int64 ExpectedRevision, int32 DayKey);
	int32 GetGuildBankAuditLogCount(const FGuid& GuildId) const;

	// --- Friends & Global Block ---
	bool SendFriendRequest(const FGuid& FromCharId, const FGuid& TargetCharId);
	bool AcceptFriendRequest(const FGuid& AccepterCharId, const FGuid& RequesterCharId);
	bool AreFriends(const FGuid& CharA, const FGuid& CharB) const;
	bool BlockCharacter(const FGuid& OwnerCharId, const FGuid& BlockedCharId);
	bool UnblockCharacter(const FGuid& OwnerCharId, const FGuid& BlockedCharId);
	bool IsBlockedBy(const FGuid& ReceiverCharId, const FGuid& SenderCharId) const;

	// --- Chat Rate-Limiting & Delivery Gate ---
	bool AdmitChatMessage(
		const FGuid& SenderCharId,
		const FGuid& OptionalRecipientCharId,
		EProject_JChatChannel Channel,
		const FString& Message,
		double NowSeconds);

	// --- Presence ---
	void UpdatePresence(const FProject_JPresenceInfo& Info);
	bool GetPresence(const FGuid& CharacterId, FProject_JPresenceInfo& OutInfo) const;

private:
	static int32 GetDailyWithdrawLimit(EProject_JGuildRank Rank);
	bool Withdraw(const FGuid& GuildId, const FGuid& ActorCharId, int32 SlotIndex, int64 ExpectedRevision,
		int32 DayKey, FGuid& OutItemInstanceId, UProject_JEconomySubsystem* Delivery);

	struct FGuildBankState
	{
		TMap<FGuid, EProject_JGuildRank> MemberRanks;
		TMap<FGuid, TPair<int32, int32>> DailyWithdrawCounts; // Character -> (current UTC day, count).
		TMap<int32, FProject_JGuildBankSlot> Slots;
		TArray<FString> AuditLogs;
		int64 LastSlotRevision = 0;
	};

	TMap<FGuid, FGuildBankState> GuildBanks;
	TMap<FGuid, TSet<FGuid>> PendingFriendRequests; // Target -> Requesters
	TMap<FGuid, TSet<FGuid>> Friends;
	TMap<FGuid, TSet<FGuid>> BlockedLists;
	TMap<TPair<FGuid, EProject_JChatChannel>, double> LastChatSendTimes;
	TMap<FGuid, FProject_JPresenceInfo> PresenceMap;
};
