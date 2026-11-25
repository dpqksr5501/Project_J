#pragma once

#include "CoreMinimal.h"
#include "MMO/EconomyLedger.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JEconomySubsystem.generated.h"

class AProject_JPlayerState;
struct FProject_JRolledLootGrant;

UENUM(BlueprintType)
enum class EProject_JAuctionStatus : uint8
{
	Active,
	Sold,
	Cancelled,
	Expired
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JAuctionListing
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	FGuid ListingId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	FGuid SellerCharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	FGuid ItemInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	FName DefinitionId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	int32 EnhanceLevel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	FName CurrencyCode = TEXT("GOLD");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	int64 BuyoutPrice = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	EProject_JAuctionStatus Status = EProject_JAuctionStatus::Active;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Auction")
	double ExpiresAtSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JMailMessage
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	FGuid MailId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	FGuid RecipientCharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	FName SubjectKey = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	FName AttachedCurrency = TEXT("GOLD");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	int64 AttachedAmount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	FGuid AttachedItemInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Economy|Mail")
	bool bClaimed = false;

	/** Auction proceeds must draw from escrow. System rewards are issued by SystemMint. */
	bool bCurrencyFromEscrow = false;
	bool bClaimPending = false;
};

/**
 * Authoritative MMORPG Economy Subsystem backed by ProjectJ::MMO::FEconomyLedger (BP01):
 *  - Auction House (escrow, search, atomic single-winner buyout, 5% fee sink, expiry mail return)
 *  - 1:1 Direct Trade (2-phase Lock -> Final Confirm, edit auto-unlocks both parties, atomic swap)
 *  - Mail System (MAIL_ESCROW atomic claim with full-inventory rollback)
 */
UCLASS()
class PROJECT_J_API UProject_JEconomySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double AuctionFeeRate = 0.05;

	virtual void Deinitialize() override;

	ProjectJ::MMO::FEconomyLedger& GetLedger() { return Ledger; }
	const ProjectJ::MMO::FEconomyLedger& GetLedger() const { return Ledger; }

	// --- Auction House ---
	bool CreateAuctionListing(
		const FGuid& IdempotencyKey,
		const FGuid& SellerCharId,
		const FGuid& ItemInstanceId,
		int64 BuyoutPrice,
		int64 ListingDepositFee,
		double DurationSeconds,
		double NowSeconds,
		FGuid& OutListingId);

	TArray<FProject_JAuctionListing> SearchActiveListings(FName OptionalDefinitionId, int32 MinEnhanceLevel, int64 MaxPrice, double NowSeconds) const;

	/** Thread-safe single-winner atomic buyout (credits seller minus fee via Mail, sends item to buyer via Mail). */
	bool ExecuteAuctionBuyout(
		const FGuid& IdempotencyKey,
		const FGuid& ListingId,
		const FGuid& BuyerCharId,
		double NowSeconds);

	/** Sweeps expired active listings back to sellers via MAIL_ESCROW. */
	int32 ProcessExpiredListings(double NowSeconds);

	// --- 1:1 Direct Trade (2-Phase Lock -> Confirm) ---
	FGuid StartDirectTrade(const FGuid& CharA, const FGuid& CharB);
	bool UpdateTradeOffer(const FGuid& TradeId, const FGuid& ActorCharId, int64 GoldOffer, const TArray<FGuid>& OfferedItemIds);
	bool SetTradeLocked(const FGuid& TradeId, const FGuid& ActorCharId, bool bLocked);
	bool ConfirmDirectTrade(const FGuid& TradeId, const FGuid& ActorCharId, const FGuid& CommitIdempotencyKey);
	bool IsTradeLocked(const FGuid& TradeId, const FGuid& ActorCharId) const;

	// --- Mail System ---
	FGuid SendSystemMail(
		const FGuid& RecipientCharId,
		FName SubjectKey,
		FName CurrencyCode,
		int64 AttachedAmount,
		const FGuid& AttachedItemInstanceId);

	/** Mints a fresh authoritative reward item directly into MailEscrow and mails it to RecipientCharId. */
	FGuid SendSystemRewardItemMail(
		const FGuid& RecipientCharId,
		FName SubjectKey,
		FName CurrencyCode,
		int64 AttachedAmount,
		FName ItemDefinitionId,
		int32 Quantity = 1);
	bool SendBossRewardMail(const FProject_JRolledLootGrant& Grant);
	bool SendGuildWithdrawalMail(const FGuid& GuildId, const FGuid& Recipient, const FGuid& ItemId, FName DefinitionId);

	/** Atomically claims mail currency + item; rolls back cleanly if bHasInventorySpace is false. */
	bool ClaimMailAttachments(const FGuid& MailId, const FGuid& RecipientCharId, bool bHasInventorySpace);
	/** Player-facing claim. Commit the exact attachment to the bound inventory before settling the local ledger. */
	bool SubmitMailClaim(const FGuid& MailId, AProject_JPlayerState* Player, TFunction<void(bool)> Completion);
	TArray<FProject_JMailMessage> GetInbox(const FGuid& RecipientCharId) const;

private:
	bool CommitMailClaim(FProject_JMailMessage& Mail, bool bHasInventorySpace);
	bool FindMailAttachment(const FProject_JMailMessage& Mail, ProjectJ::MMO::FItemOwnershipRecord& OutItem) const;
	struct FDirectTradeSession
	{
		FGuid CharA;
		FGuid CharB;
		int64 GoldA = 0, GoldB = 0;
		TArray<FGuid> ItemsA, ItemsB;
		bool bLockedA = false, bLockedB = false;
		bool bConfirmedA = false, bConfirmedB = false;
		bool bCompleted = false;
	};

	mutable FCriticalSection EconomyMutex;
	ProjectJ::MMO::FEconomyLedger Ledger;
	TMap<FGuid, FProject_JAuctionListing> Listings;
	TMap<FGuid, FProject_JMailMessage> Mails;
	TMap<FGuid, FDirectTradeSession> DirectTrades;
};
