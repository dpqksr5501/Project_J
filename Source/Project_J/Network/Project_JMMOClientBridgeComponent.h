#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Gameplay/Project_JProgressionAndLifeSubsystem.h"
#include "Social/Project_JGuildAndSocialExtensions.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "Project_JMMOClientBridgeComponent.generated.h"

class FJsonObject;
class AProject_JGameMode;
class AProject_JPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectJOnDungeonTicketReceived, const FProject_JDungeonJoinTicket&, Ticket);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectJOnMMOActionResult, FName, ActionName, bool, bSucceeded, FName, ReasonCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectJOnChatMessageReceived, FGuid, SenderCharId, EProject_JChatChannel, Channel, const FString&, Message);

USTRUCT(BlueprintType)
struct FProject_JInboxEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid MailId;
	UPROPERTY(BlueprintReadOnly) FString Subject;
	UPROPERTY(BlueprintReadOnly) FString Item;
	UPROPERTY(BlueprintReadOnly) int32 Quantity = 0;
	UPROPERTY(BlueprintReadOnly) FString Currency;
	UPROPERTY(BlueprintReadOnly) int64 Amount = 0;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProjectJOnMailInbox, const TArray<FProject_JInboxEntry>&, Entries, FGuid, Next, bool, bSucceeded);

/**
 * Modular server-authoritative RPC bridge attached to AProject_JPlayerController.
 * Routes all Phase 1–3 MMO systems (Dungeon Menu, Revive, Economy/Auction/Trade/Mail,
 * Guild Bank, Friends/Block/Chat, Gathering, Enhancement) through shared CommandBudget,
 * TrustedSession, State_Dead, and ServerIntegrity admission gates without bloating PlayerController.
 */
UCLASS(ClassGroup = (Network), meta = (BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JMMOClientBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JMMOClientBridgeComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Dungeon")
	FProjectJOnDungeonTicketReceived OnDungeonTicketReceived;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Bridge")
	FProjectJOnMMOActionResult OnMMOActionResult;

	UPROPERTY(BlueprintAssignable, Category = "MMO|Social")
	FProjectJOnChatMessageReceived OnChatMessageReceived;

	UPROPERTY(BlueprintAssignable, Category="MMO|Mail")
	FProjectJOnMailInbox OnMailInbox;

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MMO|Mail")
	void ServerRequestMailInbox(FGuid Cursor);

	UFUNCTION(Client, Reliable)
	void ClientReceiveMailInbox(const TArray<FProject_JInboxEntry>& Entries, FGuid Next, bool bSucceeded);

	// --- Dungeon Menu Entry ---
	UFUNCTION(Server, Reliable)
	void ServerRequestDungeonEntryFromMenu(FName DungeonId);

	UFUNCTION(Server, Reliable)
	void ServerRequestDungeonReturn();

	UFUNCTION(Client, Reliable)
	void ClientReceiveDungeonTicket(const FProject_JDungeonJoinTicket& Ticket, bool bAutoTravel);

	/**
	 * Shared server-authoritative dungeon entry dispatcher used by both AProject_JWorldPortal and MenuUI.
	 * Automatically gathers online party members via UProject_JSocialSubsystem, saves ReturnAnchors,
	 * allocates one shared instance, and delivers signed tickets to every member.
	 */
	static bool DispatchDungeonEntryForPlayer(
		APlayerController* RequestingPC,
		EProject_JDungeonEntrySource Source,
		FName DungeonId,
		bool bAutoTravel);

	// --- Death & Revive ---
	UFUNCTION(Server, Reliable)
	void ServerRequestRevive(EProject_JRevivePath Path);

	// --- Economy: Auction, Direct Trade, Mail ---
	UFUNCTION(Server, Reliable)
	void ServerCreateAuctionListing(FGuid IdempotencyKey, FGuid ItemInstanceId, int64 BuyoutPrice, int64 DepositFee, float DurationSeconds);

	UFUNCTION(Server, Reliable)
	void ServerBuyAuctionListing(FGuid IdempotencyKey, FGuid ListingId);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MMO|Mail")
	void ServerClaimMail(FGuid MailId);

	// --- Guild Bank, Friends, Block, Chat ---
	UFUNCTION(Server, Reliable)
	void ServerWithdrawGuildBank(FGuid GuildId, int32 SlotIndex, int64 ExpectedRevision);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MMO|Social")
	void ServerBlockCharacter(FGuid TargetCharacterId);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MMO|Social")
	void ServerUnblockCharacter(FGuid TargetCharacterId);

	void InitializeOnlineChat(const FGuid& OwnerId, int64 Epoch, const FJsonObject& Login);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MMO|Social")
	void ServerSendChatMessage(FGuid OptionalRecipientCharId, EProject_JChatChannel Channel, const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientReceiveChatMessage(FGuid SenderCharId, EProject_JChatChannel Channel, const FString& Message);

	// --- Gathering & Progression ---
	UFUNCTION(Server, Reliable)
	void ServerRequestGatherNode(FGuid NodeId, EProject_JResourceNodeMode Mode, int32 ToolTier, int32 RequiredTier, float RespawnSeconds);

	UFUNCTION(Client, Reliable)
	void ClientNotifyMMOActionResult(FName ActionName, bool bSucceeded, FName ReasonCode);

	/** Builds a standardized UE ClientTravel URL carrying the signed single-use dungeon join ticket. */
	static FString BuildDungeonTravelURL(const FProject_JDungeonJoinTicket& Ticket);

private:
	struct FOnlineDungeonEntry;
	bool BeginOnlineDungeonEntry(FName DungeonId, const TMap<FGuid, APlayerController*>& Controllers,
		const TMap<FGuid, FProject_JReturnAnchor>& Anchors, bool bAutoTravel);
	void ReadOnlineDungeonStatus(const TSharedRef<FOnlineDungeonEntry>& Entry, const TSharedPtr<FJsonObject>& Response);
	TSharedPtr<FOnlineDungeonEntry> OnlineDungeonEntry;
	bool AdmitAuthoritativePlayer(bool bRequireAlive, FGuid& OutCharacterId) const;
	void SubmitOnlineMailClaim(FGuid MailId, FGuid Character);
	FGuid MailOwner;
	int64 MailEpoch = 0;
	TSet<FGuid> VisibleClaimableMail;
	bool bReadingInbox = false;
	bool bClaimingOnlineMail = false;
	bool InstallBlockSnapshot(const FJsonObject& Snapshot);
	bool HasCurrentOnlineChat(const AProject_JGameMode* Mode, const AProject_JPlayerState* Player) const;
	void SetChatBlock(const FGuid& Target, bool bBlock);
	FGuid ChatOwner;
	int64 ChatEpoch = 0;
	int64 BlockRevision = 0;
	TSet<FGuid> BlockedSenders;
	FGuid UnconfirmedMute;
	bool bEditingBlock = false;
	FTimerHandle OnlineDungeonPollTimer;
};
