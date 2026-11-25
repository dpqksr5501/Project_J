#include "Gameplay/Project_JLootSubsystem.h"
#include "Math/RandomStream.h"

void UProject_JLootSubsystem::Deinitialize()
{
	BossRewardReceipts.Reset();
	BossRewardReceiptOrder.Reset();
	BossRewardReceiptHead = 0;
	PartyRolls.Reset();
	Super::Deinitialize();
}

FProject_JRolledLootGrant UProject_JLootSubsystem::RollPersonalLoot(
	const UProject_JLootTableDefinition* Table,
	const FGuid& RecipientCharacterId,
	int32 Seed) const
{
	FProject_JRolledLootGrant Grant;
	Grant.RecipientCharacterId = RecipientCharacterId;
	if (!Table || !RecipientCharacterId.IsValid())
	{
		return Grant;
	}

	FRandomStream Stream(Seed);
	for (const FProject_JLootTableEntry& Entry : Table->Entries)
	{
		if (Entry.ItemDefinitionId.IsNone())
		{
			continue;
		}
		if (Entry.bGuaranteed || Stream.FRand() <= FMath::Clamp(Entry.DropChance, 0.0f, 1.0f))
		{
			const int32 MinQ = FMath::Max(1, Entry.MinQuantity);
			const int32 MaxQ = FMath::Max(MinQ, Entry.MaxQuantity);
			const int32 Qty = (MinQ == MaxQ) ? MinQ : Stream.RandRange(MinQ, MaxQ);
			FProject_JLootItemStack Stack;
			Stack.ItemDefinitionId = Entry.ItemDefinitionId;
			Stack.Quantity = Qty;
			Grant.Items.Add(Stack);
		}
	}

	const int64 MinG = FMath::Max<int64>(0, Table->MinGold);
	const int64 MaxG = FMath::Max<int64>(MinG, Table->MaxGold);
	Grant.Gold = (MinG == MaxG) ? MinG : MinG + static_cast<int64>(Stream.FRand() * static_cast<double>(MaxG - MinG));
	return Grant;
}

TArray<FProject_JRolledLootGrant> UProject_JLootSubsystem::DistributeBossContributionLoot(
	FName EncounterId,
	int32 SpawnRevision,
	const UProject_JLootTableDefinition* Table,
	const TArray<FGuid>& EligibleCharacterIds,
	TFunctionRef<bool(const FGuid&, int32)> HasInventorySpace,
	int32 BaseSeed)
{
	TArray<FProject_JRolledLootGrant> Grants;
	if (EncounterId.IsNone() || !Table)
	{
		return Grants;
	}
	int32 Index = 0;
	for (const FGuid& CharId : EligibleCharacterIds)
	{
		if (!CharId.IsValid())
		{
			continue;
		}
		const FString Receipt = FString::Printf(TEXT("boss:%s:%d:%s"),
			*EncounterId.ToString(), SpawnRevision, *CharId.ToString(EGuidFormats::Digits));
		if (BossRewardReceipts.Contains(Receipt))
		{
			continue; // Idempotent guard prevents double boss rewards.
		}
		if (BossRewardReceipts.Num() >= MaxRetainedBossReceipts && BossRewardReceiptHead < BossRewardReceiptOrder.Num())
		{
			BossRewardReceipts.Remove(BossRewardReceiptOrder[BossRewardReceiptHead++]);
			if (BossRewardReceiptHead >= 4096)
			{
				BossRewardReceiptOrder.RemoveAt(0, BossRewardReceiptHead, EAllowShrinking::No);
				BossRewardReceiptHead = 0;
			}
		}
		BossRewardReceipts.Add(Receipt);
		BossRewardReceiptOrder.Add(Receipt);

		FProject_JRolledLootGrant Grant = RollPersonalLoot(Table, CharId, BaseSeed + (++Index) * 7919);
		if (!Grant.Items.IsEmpty() && !HasInventorySpace(CharId, Grant.Items.Num()))
		{
			Grant.bRoutedToMailFallback = true;
		}
		Grants.Add(MoveTemp(Grant));
	}
	return Grants;
}

FGuid UProject_JLootSubsystem::StartPartyRoll(FName ItemDefinitionId, const TArray<FGuid>& Participants, double NowSeconds)
{
	if (ItemDefinitionId.IsNone() || Participants.IsEmpty())
	{
		return FGuid();
	}
	// Evict stale expired/resolved party rolls to keep 24h server memory bounded.
	for (auto It = PartyRolls.CreateIterator(); It; ++It)
	{
		if (It.Value().bResolved || NowSeconds >= It.Value().ExpiresAt + 60.0)
		{
			It.RemoveCurrent();
		}
	}
	FPartyRollSession Session;
	Session.ItemDefinitionId = ItemDefinitionId;
	Session.ExpiresAt = NowSeconds + PartyRollTimeoutSeconds;
	for (const FGuid& P : Participants)
	{
		if (P.IsValid())
		{
			Session.Participants.Add(P);
		}
	}
	if (Session.Participants.IsEmpty())
	{
		return FGuid();
	}
	const FGuid RollId = FGuid::NewGuid();
	PartyRolls.Add(RollId, MoveTemp(Session));
	return RollId;
}

bool UProject_JLootSubsystem::SubmitPartyRollChoice(const FGuid& RollId, const FGuid& CharacterId, int32 BidPriority, int32 RollValue)
{
	FPartyRollSession* Session = PartyRolls.Find(RollId);
	if (!Session || Session->bResolved || !Session->Participants.Contains(CharacterId) || Session->Submissions.Contains(CharacterId))
	{
		return false;
	}
	Session->Submissions.Add(CharacterId, TPair<int32, int32>(FMath::Clamp(BidPriority, 0, 2), FMath::Clamp(RollValue, 1, 100)));
	return true;
}

bool UProject_JLootSubsystem::ResolvePartyRoll(const FGuid& RollId, double NowSeconds, FGuid& OutWinnerCharacterId)
{
	FPartyRollSession* Session = PartyRolls.Find(RollId);
	if (!Session || Session->bResolved)
	{
		return false;
	}
	const bool bAllSubmitted = Session->Submissions.Num() >= Session->Participants.Num();
	if (!bAllSubmitted && NowSeconds < Session->ExpiresAt)
	{
		return false;
	}

	Session->bResolved = true;
	OutWinnerCharacterId.Invalidate();
	int32 BestPriority = 0;
	int32 BestRoll = -1;
	for (const auto& Pair : Session->Submissions)
	{
		const int32 Priority = Pair.Value.Key;
		const int32 Roll = Pair.Value.Value;
		if (Priority <= 0)
		{
			continue; // Passed
		}
		if (Priority > BestPriority || (Priority == BestPriority && Roll > BestRoll))
		{
			BestPriority = Priority;
			BestRoll = Roll;
			OutWinnerCharacterId = Pair.Key;
		}
	}
	PartyRolls.Remove(RollId);
	return OutWinnerCharacterId.IsValid();
}
