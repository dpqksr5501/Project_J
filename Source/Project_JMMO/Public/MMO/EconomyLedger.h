#pragma once

#include "CoreMinimal.h"

namespace ProjectJ::MMO
{
/** Single authoritative ownership scope per globally unique item instance (dupe-proof). */
enum class EItemOwnerScope : uint8
{
	CharInv,
	CharEquip,
	AuctionEscrow,
	MailEscrow,
	GuildBank,
	Destroyed
};

enum class ELedgerStatus : uint8
{
	Committed,
	Replayed,
	InsufficientFunds,
	UnbalancedEntries,
	OwnershipConflict,
	InvalidRequest
};

struct FLedgerEntryInput
{
	FGuid AccountId;
	FName CurrencyCode;
	int64 DeltaAmount = 0; // Positive = credit, Negative = debit; sum per currency must equal 0.
};

struct FItemOwnershipRecord
{
	FGuid ItemInstanceId;
	FGuid OwnerId; // CharacterId or GuildId
	EItemOwnerScope Scope = EItemOwnerScope::CharInv;
	FName DefinitionId = NAME_None;
	int32 Quantity = 1;
	int32 EnhanceLevel = 0;
	int64 Revision = 1;
	int32 DefinitionVersion = 1;
};

struct FItemOwnershipTransfer
{
	FGuid ItemInstanceId;
	FGuid ExpectedOwnerId;
	EItemOwnerScope ExpectedScope;
	int64 ExpectedRevision;
	FGuid NewOwnerId;
	EItemOwnerScope NewScope;
};

struct FLedgerJournalEntry
{
	FGuid TransactionId;
	FGuid IdempotencyKey;
	FName Operation;
	FGuid AccountId;
	FName CurrencyCode;
	int64 DeltaAmount = 0;
	int64 BalanceAfter = 0;
};

/**
 * Pure C++ (zero-UObject) Double-Entry Economy Ledger & Single-Owner Item Registry (BP01).
 * Invariants:
 *  1. Every currency transfer enforces SUM(DeltaAmount) == 0 per currency (SystemMint/SystemSink accounts balance issuance/sinks).
 *  2. Non-system accounts can never drop below 0 balance.
 *  3. IdempotencyKey guarantees exact-once execution across retries.
 *  4. Every item_instance_id has exactly one (OwnerId, Scope, Revision) at any point in time.
 */
class PROJECT_JMMO_API FEconomyLedger
{
public:
	static FGuid SystemMintAccount();
	static FGuid SystemSinkAccount();

	/** Atomically commits a balanced double-entry currency transaction with idempotency deduplication. */
	ELedgerStatus CommitCurrencyTransaction(
		const FGuid& IdempotencyKey,
		FName Operation,
		const TArray<FLedgerEntryInput>& Entries);

	/** Validates every balance and ownership change before publishing any of them. */
	ELedgerStatus CommitTransaction(const FGuid& IdempotencyKey, FName Operation,
		TConstArrayView<FLedgerEntryInput> Entries, TConstArrayView<FItemOwnershipTransfer> Transfers);

	/** Convenience mint from SystemMintAccount -> TargetAccount (still balanced SUM==0). */
	ELedgerStatus MintCurrency(const FGuid& IdempotencyKey, const FGuid& TargetAccount, FName CurrencyCode, int64 Amount, FName Operation);

	int64 GetBalance(const FGuid& AccountId, FName CurrencyCode) const;

	/** Registers a freshly created globally unique item instance. */
	bool RegisterItemInstance(const FItemOwnershipRecord& Record);
	bool RegisterItemInstances(TConstArrayView<FItemOwnershipRecord> Records);

	/** Atomically transfers an item instance using optimistic revision CAS (prevents dupes). */
	ELedgerStatus TransferItemOwnership(
		const FGuid& ItemInstanceId,
		const FGuid& ExpectedOwnerId,
		EItemOwnerScope ExpectedScope,
		int64 ExpectedRevision,
		const FGuid& NewOwnerId,
		EItemOwnerScope NewScope);

	bool FindItem(const FGuid& ItemInstanceId, FItemOwnershipRecord& OutRecord) const;

	/** Verifies the global ledger invariant: SUM(DeltaAmount) == 0 across all journal entries per currency. */
	bool VerifyGlobalZeroSumInvariant() const;

	int32 GetJournalCount() const { return Journal.Num(); }

private:
	static bool IsSystemAccount(const FGuid& AccountId);

	mutable FCriticalSection Mutex;
	TMap<TPair<FGuid, FName>, int64> Balances;
	TMap<FGuid, FItemOwnershipRecord> Items;
	TSet<FGuid> CommittedKeys;
	TArray<FLedgerJournalEntry> Journal;
};
} // namespace ProjectJ::MMO
