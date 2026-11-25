#include "MMO/EconomyLedger.h"

namespace ProjectJ::MMO
{
namespace
{
bool TryAdd(int64& Total, int64 Value)
{
	if ((Value > 0 && Total > MAX_int64 - Value) || (Value < 0 && Total < MIN_int64 - Value)) return false;
	Total += Value;
	return true;
}
}

FGuid FEconomyLedger::SystemMintAccount()
{
	return FGuid(0x00000001, 0x00000000, 0x00000000, 0x00000001);
}

FGuid FEconomyLedger::SystemSinkAccount()
{
	return FGuid(0x00000002, 0x00000000, 0x00000000, 0x00000002);
}

bool FEconomyLedger::IsSystemAccount(const FGuid& AccountId)
{
	return AccountId == SystemMintAccount() || AccountId == SystemSinkAccount();
}

ELedgerStatus FEconomyLedger::CommitCurrencyTransaction(
	const FGuid& IdempotencyKey,
	FName Operation,
	const TArray<FLedgerEntryInput>& Entries)
{
	return CommitTransaction(IdempotencyKey, Operation, Entries, {});
}

ELedgerStatus FEconomyLedger::CommitTransaction(const FGuid& IdempotencyKey, FName Operation,
	TConstArrayView<FLedgerEntryInput> Entries, TConstArrayView<FItemOwnershipTransfer> Transfers)
{
	if (!IdempotencyKey.IsValid() || Operation.IsNone() || (Entries.IsEmpty() && Transfers.IsEmpty()) || Entries.Num() == 1)
	{
		return ELedgerStatus::InvalidRequest;
	}

	FScopeLock Lock(&Mutex);
	if (CommittedKeys.Contains(IdempotencyKey))
	{
		return ELedgerStatus::Replayed;
	}

	TSet<FGuid> TouchedItems;
	for (const FItemOwnershipTransfer& Transfer : Transfers)
	{
		if (!Transfer.ItemInstanceId.IsValid() || !Transfer.NewOwnerId.IsValid()
			|| TouchedItems.Contains(Transfer.ItemInstanceId)) return ELedgerStatus::InvalidRequest;
		TouchedItems.Add(Transfer.ItemInstanceId);
		const auto* Record = Items.Find(Transfer.ItemInstanceId);
		if (!Record || Record->OwnerId != Transfer.ExpectedOwnerId || Record->Scope != Transfer.ExpectedScope
			|| Record->Revision != Transfer.ExpectedRevision || Record->Revision == MAX_int64) return ELedgerStatus::OwnershipConflict;
	}

	// Validate the same balances that will be recorded in journal order.
	TMap<FName, int64> CurrencySums;
	TMap<TPair<FGuid, FName>, int64> UpdatedBalances;
	for (const FLedgerEntryInput& Entry : Entries)
	{
		if (!Entry.AccountId.IsValid() || Entry.CurrencyCode.IsNone() || Entry.DeltaAmount == 0)
		{
			return ELedgerStatus::InvalidRequest;
		}
		const TPair<FGuid, FName> Key(Entry.AccountId, Entry.CurrencyCode);
		int64* Balance = UpdatedBalances.Find(Key);
		if (!Balance) Balance = &UpdatedBalances.Add(Key, Balances.FindRef(Key));
		if (!TryAdd(CurrencySums.FindOrAdd(Entry.CurrencyCode), Entry.DeltaAmount) || !TryAdd(*Balance, Entry.DeltaAmount))
			return ELedgerStatus::InvalidRequest;
	}
	for (const auto& Pair : CurrencySums)
	{
		if (Pair.Value != 0)
		{
			return ELedgerStatus::UnbalancedEntries;
		}
	}

	// 2. Verify non-system accounts never go negative.
	for (const auto& Pair : UpdatedBalances)
	{
		if (!IsSystemAccount(Pair.Key.Key) && Pair.Value < 0) return ELedgerStatus::InsufficientFunds;
	}

	// 3. Commit atomically & append journal entries.
	const FGuid TxId = FGuid::NewGuid();
	for (const FLedgerEntryInput& Entry : Entries)
	{
		const TPair<FGuid, FName> Key(Entry.AccountId, Entry.CurrencyCode);
		int64& Bal = Balances.FindOrAdd(Key);
		Bal += Entry.DeltaAmount;

		FLedgerJournalEntry Row;
		Row.TransactionId = TxId;
		Row.IdempotencyKey = IdempotencyKey;
		Row.Operation = Operation;
		Row.AccountId = Entry.AccountId;
		Row.CurrencyCode = Entry.CurrencyCode;
		Row.DeltaAmount = Entry.DeltaAmount;
		Row.BalanceAfter = Bal;
		Journal.Add(Row);
	}
	for (const FItemOwnershipTransfer& Transfer : Transfers)
	{
		auto& Record = Items.FindChecked(Transfer.ItemInstanceId);
		Record.OwnerId = Transfer.NewOwnerId;
		Record.Scope = Transfer.NewScope;
		++Record.Revision;
	}
	CommittedKeys.Add(IdempotencyKey);
	return ELedgerStatus::Committed;
}

ELedgerStatus FEconomyLedger::MintCurrency(
	const FGuid& IdempotencyKey,
	const FGuid& TargetAccount,
	FName CurrencyCode,
	int64 Amount,
	FName Operation)
{
	if (Amount <= 0 || !TargetAccount.IsValid())
	{
		return ELedgerStatus::InvalidRequest;
	}
	TArray<FLedgerEntryInput> Entries;
	Entries.Add({ SystemMintAccount(), CurrencyCode, -Amount });
	Entries.Add({ TargetAccount, CurrencyCode, Amount });
	return CommitCurrencyTransaction(IdempotencyKey, Operation, Entries);
}

int64 FEconomyLedger::GetBalance(const FGuid& AccountId, FName CurrencyCode) const
{
	FScopeLock Lock(&Mutex);
	return Balances.FindRef(TPair<FGuid, FName>(AccountId, CurrencyCode));
}

bool FEconomyLedger::RegisterItemInstance(const FItemOwnershipRecord& Record)
{
	return RegisterItemInstances(MakeArrayView(&Record, 1));
}

bool FEconomyLedger::RegisterItemInstances(TConstArrayView<FItemOwnershipRecord> Records)
{
	FScopeLock Lock(&Mutex);
	TSet<FGuid> RegisteredIds;
	for (const FItemOwnershipRecord& Record : Records)
	{
		if (!Record.ItemInstanceId.IsValid() || !Record.OwnerId.IsValid() || Record.DefinitionId.IsNone()
			|| Record.Quantity <= 0 || Record.DefinitionVersion < 1 || Record.Revision < 1
			|| Items.Contains(Record.ItemInstanceId) || RegisteredIds.Contains(Record.ItemInstanceId)) return false;
		RegisteredIds.Add(Record.ItemInstanceId);
	}
	for (const FItemOwnershipRecord& Record : Records) Items.Add(Record.ItemInstanceId, Record);
	return true;
}

ELedgerStatus FEconomyLedger::TransferItemOwnership(
	const FGuid& ItemInstanceId,
	const FGuid& ExpectedOwnerId,
	EItemOwnerScope ExpectedScope,
	int64 ExpectedRevision,
	const FGuid& NewOwnerId,
	EItemOwnerScope NewScope)
{
	if (!ItemInstanceId.IsValid() || !NewOwnerId.IsValid())
	{
		return ELedgerStatus::InvalidRequest;
	}
	FScopeLock Lock(&Mutex);
	FItemOwnershipRecord* Rec = Items.Find(ItemInstanceId);
	if (!Rec || Rec->OwnerId != ExpectedOwnerId || Rec->Scope != ExpectedScope || Rec->Revision != ExpectedRevision || Rec->Revision == MAX_int64)
	{
		return ELedgerStatus::OwnershipConflict;
	}
	Rec->OwnerId = NewOwnerId;
	Rec->Scope = NewScope;
	++Rec->Revision;
	return ELedgerStatus::Committed;
}

bool FEconomyLedger::FindItem(const FGuid& ItemInstanceId, FItemOwnershipRecord& OutRecord) const
{
	FScopeLock Lock(&Mutex);
	if (const FItemOwnershipRecord* Found = Items.Find(ItemInstanceId))
	{
		OutRecord = *Found;
		return true;
	}
	return false;
}

bool FEconomyLedger::VerifyGlobalZeroSumInvariant() const
{
	FScopeLock Lock(&Mutex);
	TMap<FName, int64> Sums;
	for (const FLedgerJournalEntry& Row : Journal)
	{
		if (!TryAdd(Sums.FindOrAdd(Row.CurrencyCode), Row.DeltaAmount)) return false;
	}
	for (const auto& Pair : Sums)
	{
		if (Pair.Value != 0)
		{
			return false;
		}
	}
	return true;
}
} // namespace ProjectJ::MMO
