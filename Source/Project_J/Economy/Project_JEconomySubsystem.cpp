#include "Economy/Project_JEconomySubsystem.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Game/Project_JPlayerState.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "Inventory/Project_JItemDefinition.h"
#include "UObject/StrongObjectPtr.h"

using namespace ProjectJ::MMO;

namespace
{
const FGuid MailEscrowAccount(0x00000003, 0, 0, 0x00000003);
}

void UProject_JEconomySubsystem::Deinitialize()
{
	FScopeLock Lock(&EconomyMutex);
	Listings.Reset();
	Mails.Reset();
	DirectTrades.Reset();
	Super::Deinitialize();
}

bool UProject_JEconomySubsystem::CreateAuctionListing(
	const FGuid& IdempotencyKey,
	const FGuid& SellerCharId,
	const FGuid& ItemInstanceId,
	int64 BuyoutPrice,
	int64 ListingDepositFee,
	double DurationSeconds,
	double NowSeconds,
	FGuid& OutListingId)
{
	if (!IdempotencyKey.IsValid() || !SellerCharId.IsValid() || !ItemInstanceId.IsValid()
		|| BuyoutPrice <= 0 || ListingDepositFee < 0 || !FMath::IsFinite(NowSeconds)
		|| !FMath::IsFinite(NowSeconds + DurationSeconds) || DurationSeconds <= 0.0)
	{
		return false;
	}

	FScopeLock Lock(&EconomyMutex);
	FItemOwnershipRecord Item;
	if (!Ledger.FindItem(ItemInstanceId, Item) || Item.OwnerId != SellerCharId || Item.Scope != EItemOwnerScope::CharInv)
	{
		return false;
	}

	TArray<FLedgerEntryInput> FeeEntries;
	if (ListingDepositFee > 0)
	{
		FeeEntries.Add({ SellerCharId, TEXT("GOLD"), -ListingDepositFee });
		FeeEntries.Add({ FEconomyLedger::SystemSinkAccount(), TEXT("GOLD"), ListingDepositFee });
	}
	const FItemOwnershipTransfer Transfer{ItemInstanceId, SellerCharId, EItemOwnerScope::CharInv, Item.Revision,
		SellerCharId, EItemOwnerScope::AuctionEscrow};
	if (Ledger.CommitTransaction(IdempotencyKey, TEXT("AuctionDeposit"), FeeEntries, MakeArrayView(&Transfer, 1)) != ELedgerStatus::Committed)
	{
		return false;
	}

	FProject_JAuctionListing Listing;
	Listing.ListingId = FGuid::NewGuid();
	Listing.SellerCharacterId = SellerCharId;
	Listing.ItemInstanceId = ItemInstanceId;
	Listing.DefinitionId = Item.DefinitionId;
	Listing.EnhanceLevel = Item.EnhanceLevel;
	Listing.BuyoutPrice = BuyoutPrice;
	Listing.Status = EProject_JAuctionStatus::Active;
	Listing.ExpiresAtSeconds = NowSeconds + DurationSeconds;
	OutListingId = Listing.ListingId;
	Listings.Add(Listing.ListingId, MoveTemp(Listing));
	return true;
}

TArray<FProject_JAuctionListing> UProject_JEconomySubsystem::SearchActiveListings(
	FName OptionalDefinitionId,
	int32 MinEnhanceLevel,
	int64 MaxPrice,
	double NowSeconds) const
{
	FScopeLock Lock(&EconomyMutex);
	TArray<FProject_JAuctionListing> Results;
	for (const auto& Pair : Listings)
	{
		const FProject_JAuctionListing& L = Pair.Value;
		if (L.Status != EProject_JAuctionStatus::Active || NowSeconds > L.ExpiresAtSeconds)
		{
			continue;
		}
		if (!OptionalDefinitionId.IsNone() && L.DefinitionId != OptionalDefinitionId)
		{
			continue;
		}
		if (L.EnhanceLevel < MinEnhanceLevel)
		{
			continue;
		}
		if (MaxPrice > 0 && L.BuyoutPrice > MaxPrice)
		{
			continue;
		}
		Results.Add(L);
	}
	Results.Sort([](const FProject_JAuctionListing& A, const FProject_JAuctionListing& B)
	{
		return A.BuyoutPrice < B.BuyoutPrice;
	});
	return Results;
}

bool UProject_JEconomySubsystem::ExecuteAuctionBuyout(
	const FGuid& IdempotencyKey,
	const FGuid& ListingId,
	const FGuid& BuyerCharId,
	double NowSeconds)
{
	if (!IdempotencyKey.IsValid() || !ListingId.IsValid() || !BuyerCharId.IsValid() || !FMath::IsFinite(NowSeconds))
	{
		return false;
	}

	// Single-winner lock serializes concurrent buyouts (equivalent to PostgreSQL SELECT FOR UPDATE SKIP LOCKED).
	FScopeLock Lock(&EconomyMutex);
	FProject_JAuctionListing* Listing = Listings.Find(ListingId);
	if (!Listing || Listing->Status != EProject_JAuctionStatus::Active || NowSeconds > Listing->ExpiresAtSeconds
		|| Listing->SellerCharacterId == BuyerCharId)
	{
		return false;
	}

	FItemOwnershipRecord Item;
	if (!Ledger.FindItem(Listing->ItemInstanceId, Item) || Item.Scope != EItemOwnerScope::AuctionEscrow
		|| Item.OwnerId != Listing->SellerCharacterId)
	{
		return false;
	}

	const int64 Fee = FMath::Max<int64>(1, static_cast<int64>(Listing->BuyoutPrice * AuctionFeeRate));
	const int64 SellerPayout = FMath::Max<int64>(0, Listing->BuyoutPrice - Fee);

	TArray<FLedgerEntryInput> Entries;
	Entries.Add({ BuyerCharId, Listing->CurrencyCode, -Listing->BuyoutPrice });
	if (SellerPayout > 0)
	{
		Entries.Add({ MailEscrowAccount, Listing->CurrencyCode, SellerPayout });
	}
	Entries.Add({ FEconomyLedger::SystemSinkAccount(), Listing->CurrencyCode, Fee });

	const FItemOwnershipTransfer Transfer{Item.ItemInstanceId, Item.OwnerId, EItemOwnerScope::AuctionEscrow, Item.Revision,
		BuyerCharId, EItemOwnerScope::MailEscrow};
	if (Ledger.CommitTransaction(IdempotencyKey, TEXT("AuctionBuyout"), Entries, MakeArrayView(&Transfer, 1)) != ELedgerStatus::Committed)
	{
		return false;
	}

	Listing->Status = EProject_JAuctionStatus::Sold;

	// Deliver item to buyer and net proceeds to seller via Mail.
	FProject_JMailMessage BuyerMail;
	BuyerMail.MailId = FGuid::NewGuid();
	BuyerMail.RecipientCharacterId = BuyerCharId;
	BuyerMail.SubjectKey = TEXT("MAIL_AUCTION_WON");
	BuyerMail.AttachedItemInstanceId = Listing->ItemInstanceId;
	Mails.Add(BuyerMail.MailId, BuyerMail);

	FProject_JMailMessage SellerMail;
	SellerMail.MailId = FGuid::NewGuid();
	SellerMail.RecipientCharacterId = Listing->SellerCharacterId;
	SellerMail.SubjectKey = TEXT("MAIL_AUCTION_SOLD");
	SellerMail.AttachedCurrency = Listing->CurrencyCode;
	SellerMail.AttachedAmount = SellerPayout;
	SellerMail.bCurrencyFromEscrow = true;
	Mails.Add(SellerMail.MailId, SellerMail);
	return true;
}

int32 UProject_JEconomySubsystem::ProcessExpiredListings(double NowSeconds)
{
	FScopeLock Lock(&EconomyMutex);
	int32 ExpiredCount = 0;
	for (auto& Pair : Listings)
	{
		FProject_JAuctionListing& L = Pair.Value;
		if (L.Status == EProject_JAuctionStatus::Active && NowSeconds > L.ExpiresAtSeconds)
		{
			FItemOwnershipRecord Item;
			if (!Ledger.FindItem(L.ItemInstanceId, Item) || Item.OwnerId != L.SellerCharacterId
				|| Ledger.TransferItemOwnership(Item.ItemInstanceId, L.SellerCharacterId, EItemOwnerScope::AuctionEscrow, Item.Revision,
					L.SellerCharacterId, EItemOwnerScope::MailEscrow) != ELedgerStatus::Committed) continue;
			L.Status = EProject_JAuctionStatus::Expired;
			FProject_JMailMessage ReturnMail;
			ReturnMail.MailId = FGuid::NewGuid();
			ReturnMail.RecipientCharacterId = L.SellerCharacterId;
			ReturnMail.SubjectKey = TEXT("MAIL_AUCTION_EXPIRED");
			ReturnMail.AttachedItemInstanceId = L.ItemInstanceId;
			Mails.Add(ReturnMail.MailId, ReturnMail);
			++ExpiredCount;
		}
	}
	return ExpiredCount;
}

FGuid UProject_JEconomySubsystem::StartDirectTrade(const FGuid& CharA, const FGuid& CharB)
{
	if (!CharA.IsValid() || !CharB.IsValid() || CharA == CharB)
	{
		return FGuid();
	}
	FScopeLock Lock(&EconomyMutex);
	FDirectTradeSession Session;
	Session.CharA = CharA;
	Session.CharB = CharB;
	const FGuid TradeId = FGuid::NewGuid();
	DirectTrades.Add(TradeId, MoveTemp(Session));
	return TradeId;
}

bool UProject_JEconomySubsystem::UpdateTradeOffer(const FGuid& TradeId, const FGuid& ActorCharId, int64 GoldOffer, const TArray<FGuid>& OfferedItemIds)
{
	if (GoldOffer < 0)
	{
		return false;
	}
	TSet<FGuid> UniqueItems;
	UniqueItems.Reserve(OfferedItemIds.Num());
	for (const FGuid& Id : OfferedItemIds)
	{
		if (!Id.IsValid() || UniqueItems.Contains(Id))
		{
			return false; // Reject invalid or duplicate item instance IDs in one trade offer.
		}
		UniqueItems.Add(Id);
	}
	FScopeLock Lock(&EconomyMutex);
	FDirectTradeSession* Session = DirectTrades.Find(TradeId);
	if (!Session || Session->bCompleted)
	{
		return false;
	}
	if (ActorCharId == Session->CharA)
	{
		Session->GoldA = GoldOffer;
		Session->ItemsA = OfferedItemIds;
	}
	else if (ActorCharId == Session->CharB)
	{
		Session->GoldB = GoldOffer;
		Session->ItemsB = OfferedItemIds;
	}
	else
	{
		return false;
	}
	// Anti-scam rule (BP01): any edit automatically unlocks & unconfirms BOTH participants.
	Session->bLockedA = Session->bLockedB = false;
	Session->bConfirmedA = Session->bConfirmedB = false;
	return true;
}

bool UProject_JEconomySubsystem::SetTradeLocked(const FGuid& TradeId, const FGuid& ActorCharId, bool bLocked)
{
	FScopeLock Lock(&EconomyMutex);
	FDirectTradeSession* Session = DirectTrades.Find(TradeId);
	if (!Session || Session->bCompleted)
	{
		return false;
	}
	if (ActorCharId == Session->CharA) Session->bLockedA = bLocked;
	else if (ActorCharId == Session->CharB) Session->bLockedB = bLocked;
	else return false;

	if (!bLocked)
	{
		Session->bConfirmedA = Session->bConfirmedB = false;
	}
	return true;
}

bool UProject_JEconomySubsystem::IsTradeLocked(const FGuid& TradeId, const FGuid& ActorCharId) const
{
	FScopeLock Lock(&EconomyMutex);
	const FDirectTradeSession* Session = DirectTrades.Find(TradeId);
	if (!Session) return false;
	if (ActorCharId == Session->CharA) return Session->bLockedA;
	if (ActorCharId == Session->CharB) return Session->bLockedB;
	return false;
}

bool UProject_JEconomySubsystem::ConfirmDirectTrade(const FGuid& TradeId, const FGuid& ActorCharId, const FGuid& CommitIdempotencyKey)
{
	FScopeLock Lock(&EconomyMutex);
	FDirectTradeSession* Session = DirectTrades.Find(TradeId);
	if (!Session || Session->bCompleted || !Session->bLockedA || !Session->bLockedB)
	{
		return false;
	}
	const bool bPrevA = Session->bConfirmedA;
	const bool bPrevB = Session->bConfirmedB;
	if (ActorCharId == Session->CharA) Session->bConfirmedA = true;
	else if (ActorCharId == Session->CharB) Session->bConfirmedB = true;
	else return false;

	if (!Session->bConfirmedA || !Session->bConfirmedB)
	{
		return true; // Waiting on counterparty confirmation.
	}

	auto RollbackConfirmFlags = [&]()
	{
		Session->bConfirmedA = bPrevA;
		Session->bConfirmedB = bPrevB;
	};

	// Pre-validate all offered items before mutating anything.
	TArray<FItemOwnershipTransfer> Transfers;
	for (const FGuid& Id : Session->ItemsA)
	{
		FItemOwnershipRecord Rec;
		if (!Ledger.FindItem(Id, Rec) || Rec.OwnerId != Session->CharA || Rec.Scope != EItemOwnerScope::CharInv)
		{
			RollbackConfirmFlags();
			return false;
		}
		Transfers.Add({Id, Session->CharA, EItemOwnerScope::CharInv, Rec.Revision, Session->CharB, EItemOwnerScope::CharInv});
	}
	for (const FGuid& Id : Session->ItemsB)
	{
		FItemOwnershipRecord Rec;
		if (!Ledger.FindItem(Id, Rec) || Rec.OwnerId != Session->CharB || Rec.Scope != EItemOwnerScope::CharInv)
		{
			RollbackConfirmFlags();
			return false;
		}
		Transfers.Add({Id, Session->CharB, EItemOwnerScope::CharInv, Rec.Revision, Session->CharA, EItemOwnerScope::CharInv});
	}

	const int64 NetGoldAToB = Session->GoldA - Session->GoldB;
	TArray<FLedgerEntryInput> Entries;
	if (NetGoldAToB != 0)
	{
		Entries.Add({ Session->CharA, TEXT("GOLD"), -NetGoldAToB });
		Entries.Add({ Session->CharB, TEXT("GOLD"), NetGoldAToB });
	}
	const ELedgerStatus CommitStatus = Ledger.CommitTransaction(CommitIdempotencyKey, TEXT("DirectTrade"), Entries, Transfers);
	if (CommitStatus != ELedgerStatus::Committed && CommitStatus != ELedgerStatus::Replayed)
	{
		RollbackConfirmFlags();
		return false;
	}
	Session->bCompleted = true;
	return true;
}

FGuid UProject_JEconomySubsystem::SendSystemMail(
	const FGuid& RecipientCharId,
	FName SubjectKey,
	FName CurrencyCode,
	int64 AttachedAmount,
	const FGuid& AttachedItemInstanceId)
{
	if (!RecipientCharId.IsValid() || SubjectKey.IsNone() || AttachedAmount < 0 || (AttachedAmount > 0 && CurrencyCode.IsNone()))
	{
		return FGuid();
	}
	FScopeLock Lock(&EconomyMutex);
	FProject_JMailMessage Msg;
	Msg.MailId = FGuid::NewGuid();
	Msg.RecipientCharacterId = RecipientCharId;
	Msg.SubjectKey = SubjectKey;
	Msg.AttachedCurrency = CurrencyCode;
	Msg.AttachedAmount = AttachedAmount;
	Msg.AttachedItemInstanceId = AttachedItemInstanceId;
	Mails.Add(Msg.MailId, Msg);
	return Msg.MailId;
}

FGuid UProject_JEconomySubsystem::SendSystemRewardItemMail(
	const FGuid& RecipientCharId,
	FName SubjectKey,
	FName CurrencyCode,
	int64 AttachedAmount,
	FName ItemDefinitionId,
	int32 Quantity)
{
	if (!RecipientCharId.IsValid() || SubjectKey.IsNone() || AttachedAmount < 0 || (AttachedAmount > 0 && CurrencyCode.IsNone())
		|| ItemDefinitionId.IsNone() || Quantity <= 0)
	{
		return FGuid();
	}
	FGuid NewItemId;
	{
		FScopeLock Lock(&EconomyMutex);
		FItemOwnershipRecord Rec;
		Rec.ItemInstanceId = FGuid::NewGuid();
		Rec.OwnerId = RecipientCharId;
		Rec.Scope = EItemOwnerScope::MailEscrow;
		Rec.DefinitionId = ItemDefinitionId;
		Rec.Quantity = Quantity;
		Rec.EnhanceLevel = 0;
		Rec.Revision = 1;
		if (!Ledger.RegisterItemInstance(Rec))
		{
			return FGuid();
		}
		NewItemId = Rec.ItemInstanceId;
	}
	return SendSystemMail(RecipientCharId, SubjectKey, CurrencyCode, AttachedAmount, NewItemId);
}

bool UProject_JEconomySubsystem::SendBossRewardMail(const FProject_JRolledLootGrant& Grant)
{
	if (!Grant.RecipientCharacterId.IsValid() || Grant.Gold < 0) return false;
	TArray<FItemOwnershipRecord> Items;
	Items.Reserve(Grant.Items.Num());
	for (const FProject_JLootItemStack& Stack : Grant.Items)
	{
		FItemOwnershipRecord& Item = Items.Emplace_GetRef();
		Item.ItemInstanceId = FGuid::NewGuid();
		Item.OwnerId = Grant.RecipientCharacterId;
		Item.Scope = EItemOwnerScope::MailEscrow;
		Item.DefinitionId = Stack.ItemDefinitionId;
		Item.Quantity = Stack.Quantity;
		Item.DefinitionVersion = Stack.ItemDefinitionVersion;
	}
	FScopeLock Lock(&EconomyMutex);
	if (!Ledger.RegisterItemInstances(Items)) return false;
	for (const FItemOwnershipRecord& Item : Items)
	{
		FProject_JMailMessage Mail;
		Mail.MailId = FGuid::NewGuid();
		Mail.RecipientCharacterId = Grant.RecipientCharacterId;
		Mail.SubjectKey = TEXT("MAIL_FIELDBOSS_REWARD");
		Mail.AttachedItemInstanceId = Item.ItemInstanceId;
		Mails.Add(Mail.MailId, MoveTemp(Mail));
	}
	if (Grant.Gold > 0)
	{
		FProject_JMailMessage Mail;
		Mail.MailId = FGuid::NewGuid();
		Mail.RecipientCharacterId = Grant.RecipientCharacterId;
		Mail.SubjectKey = TEXT("MAIL_FIELDBOSS_REWARD");
		Mail.AttachedAmount = Grant.Gold;
		Mails.Add(Mail.MailId, MoveTemp(Mail));
	}
	return true;
}

bool UProject_JEconomySubsystem::SendGuildWithdrawalMail(const FGuid& GuildId, const FGuid& Recipient,
	const FGuid& ItemId, FName DefinitionId)
{
	FScopeLock Lock(&EconomyMutex);
	FItemOwnershipRecord Item;
	if (!Ledger.FindItem(ItemId, Item) || Item.OwnerId != GuildId || Item.DefinitionId != DefinitionId
		|| Ledger.TransferItemOwnership(ItemId, GuildId, EItemOwnerScope::GuildBank, Item.Revision,
			Recipient, EItemOwnerScope::MailEscrow) != ELedgerStatus::Committed) return false;
	FProject_JMailMessage Mail;
	Mail.MailId = FGuid::NewGuid();
	Mail.SubjectKey = TEXT("MAIL_GUILD_WITHDRAWAL");
	Mail.RecipientCharacterId = Recipient;
	Mail.AttachedItemInstanceId = ItemId;
	Mails.Add(Mail.MailId, MoveTemp(Mail));
	return true;
}

bool UProject_JEconomySubsystem::ClaimMailAttachments(const FGuid& MailId, const FGuid& RecipientCharId, bool bHasInventorySpace)
{
	FScopeLock Lock(&EconomyMutex);
	FProject_JMailMessage* Msg = Mails.Find(MailId);
	return Msg && !Msg->bClaimed && !Msg->bClaimPending && Msg->RecipientCharacterId == RecipientCharId
		&& CommitMailClaim(*Msg, bHasInventorySpace);
}

bool UProject_JEconomySubsystem::FindMailAttachment(const FProject_JMailMessage& Mail, FItemOwnershipRecord& OutItem) const
{
	return Ledger.FindItem(Mail.AttachedItemInstanceId, OutItem) && OutItem.Scope == EItemOwnerScope::MailEscrow
		&& OutItem.OwnerId == Mail.RecipientCharacterId;
}


bool UProject_JEconomySubsystem::CommitMailClaim(FProject_JMailMessage& Mail, bool bHasInventorySpace)
{
	const FGuid RecipientCharId = Mail.RecipientCharacterId;
	FItemOwnershipRecord PendingItem;
	const bool bHasItemAttachment = Mail.AttachedItemInstanceId.IsValid();
	if (bHasItemAttachment && (!bHasInventorySpace || !FindMailAttachment(Mail, PendingItem))) return false;

	TArray<FLedgerEntryInput> Entries;
	if (Mail.AttachedAmount > 0)
	{
		const FGuid Source = Mail.bCurrencyFromEscrow ? MailEscrowAccount : FEconomyLedger::SystemMintAccount();
		Entries.Add({ Source, Mail.AttachedCurrency, -Mail.AttachedAmount });
		Entries.Add({ RecipientCharId, Mail.AttachedCurrency, Mail.AttachedAmount });
	}
	const FItemOwnershipTransfer Transfer{PendingItem.ItemInstanceId, RecipientCharId, EItemOwnerScope::MailEscrow, PendingItem.Revision,
		RecipientCharId, EItemOwnerScope::CharInv};
	const auto Transfers = MakeArrayView(&Transfer, bHasItemAttachment ? 1 : 0);
	if ((!Entries.IsEmpty() || !Transfers.IsEmpty())
		&& Ledger.CommitTransaction(FGuid::NewGuid(), TEXT("MailClaim"), Entries, Transfers) != ELedgerStatus::Committed) return false;

	Mail.bClaimed = true;
	return true;
}

bool UProject_JEconomySubsystem::SubmitMailClaim(const FGuid& MailId, AProject_JPlayerState* Player, TFunction<void(bool)> Completion)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(Player);
	if (!IsValid(Player) || Player->GetGameInstance() != GetGameInstance() || !Player->HasAuthority()
		|| !Player->CanAcceptSessionCommand() || !Player->GetCharacterId().IsValid()) return false;
	const FGuid Character = Player->GetCharacterId().Value;
	FItemOwnershipRecord Item;
	bool bCurrencyOnly = false;
	{
		FScopeLock Lock(&EconomyMutex);
		auto* Mail = Mails.Find(MailId);
		if (!Mail || Mail->bClaimed || Mail->bClaimPending || Mail->RecipientCharacterId != Character) return false;
		bCurrencyOnly = !Mail->AttachedItemInstanceId.IsValid();
		if (bCurrencyOnly)
		{
			if (!CommitMailClaim(*Mail, true)) return false;
		}
		else if (!FindMailAttachment(*Mail, Item)) return false;
		else Mail->bClaimPending = true;
	}
	if (bCurrencyOnly)
	{
		if (Completion) Completion(true);
		return true;
	}

	auto* Profiles = GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>();
	auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto* Definition = Data ? Data->GetItemDefinition(Item.DefinitionId, Item.DefinitionVersion) : nullptr;
	if (Profiles && Definition)
	{
		FProject_JPersistentEdit Edit;
		Edit.Kind = EProject_JPersistentEdit::Add;
		Edit.Definition = Definition;
		Edit.Amount = Item.Quantity;
		Edit.Level = FMath::Max(1, Item.EnhanceLevel);
		Edit.Instance = Item.ItemInstanceId;
		Edit.Request = Profiles->NewRequestId(Player);
		const TWeakObjectPtr<ThisClass> WeakSelf(this);
		auto OnProjection = [WeakSelf, MailId, Completion = MoveTemp(Completion)](bool bProjected, bool)
		{
			if (!Completion) return;
			bool bSettled = false;
			if (auto* Self = WeakSelf.Get())
			{
				FScopeLock Lock(&Self->EconomyMutex);
				const auto* Mail = Self->Mails.Find(MailId);
				bSettled = Mail && Mail->bClaimed;
			}
			Completion(bProjected && bSettled);
		};
		if (Profiles->SubmitEdit(Player, Edit, MoveTemp(OnProjection), [WeakSelf, MailId](const FProject_JProfileDurableResult& Result)
		{
			auto* Self = WeakSelf.Get();
			if (!Self) return;
			bool bClaimed = false;
			{
				FScopeLock Lock(&Self->EconomyMutex);
				auto* Mail = Self->Mails.Find(MailId);
				if (!Mail || !Mail->bClaimPending || Mail->bClaimed) return;
				if (Result.Outcome == EProject_JProfileDurableOutcome::Committed)
					bClaimed = Self->CommitMailClaim(*Mail, true);
				// A possibly committed grant must never be submitted again as a fresh request.
				Mail->bClaimPending = !bClaimed && Result.Outcome != EProject_JProfileDurableOutcome::ProvenAbsent;
			}
		})) return true;
	}
	FScopeLock Lock(&EconomyMutex);
	if (auto* Mail = Mails.Find(MailId)) Mail->bClaimPending = false;
	return false;
}

TArray<FProject_JMailMessage> UProject_JEconomySubsystem::GetInbox(const FGuid& RecipientCharId) const
{
	FScopeLock Lock(&EconomyMutex);
	TArray<FProject_JMailMessage> Out;
	for (const auto& Pair : Mails)
	{
		if (Pair.Value.RecipientCharacterId == RecipientCharId)
		{
			Out.Add(Pair.Value);
		}
	}
	return Out;
}
