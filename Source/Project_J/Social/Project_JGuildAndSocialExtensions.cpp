#include "Social/Project_JGuildAndSocialExtensions.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Engine/GameInstance.h"

void UProject_JGuildAndSocialExtensionsSubsystem::Deinitialize()
{
	GuildBanks.Reset();
	PendingFriendRequests.Reset();
	Friends.Reset();
	BlockedLists.Reset();
	LastChatSendTimes.Reset();
	PresenceMap.Reset();
	Super::Deinitialize();
}

int32 UProject_JGuildAndSocialExtensionsSubsystem::GetDailyWithdrawLimit(EProject_JGuildRank Rank)
{
	switch (Rank)
	{
	case EProject_JGuildRank::Initiate: return 0;
	case EProject_JGuildRank::Member: return 3;
	case EProject_JGuildRank::Officer: return 20;
	case EProject_JGuildRank::Master: return 999;
	default: return 0;
	}
}

void UProject_JGuildAndSocialExtensionsSubsystem::SetGuildMemberRank(const FGuid& GuildId, const FGuid& CharacterId, EProject_JGuildRank Rank)
{
	if (GuildId.IsValid() && CharacterId.IsValid())
	{
		GuildBanks.FindOrAdd(GuildId).MemberRanks.Add(CharacterId, Rank);
	}
}

bool UProject_JGuildAndSocialExtensionsSubsystem::DepositToGuildBank(
	const FGuid& GuildId,
	const FGuid& ActorCharId,
	int32 SlotIndex,
	const FGuid& ItemInstanceId,
	FName DefinitionId)
{
	check(IsInGameThread());
	if (!GuildId.IsValid() || !ActorCharId.IsValid() || SlotIndex < 0 || SlotIndex >= 256 || !ItemInstanceId.IsValid() || DefinitionId.IsNone())
	{
		return false;
	}
	FGuildBankState* Bank = GuildBanks.Find(GuildId);
	if (!Bank || !Bank->MemberRanks.Contains(ActorCharId) || Bank->Slots.Contains(SlotIndex) || Bank->LastSlotRevision == MAX_int64)
	{
		return false;
	}
	FProject_JGuildBankSlot Slot;
	Slot.SlotIndex = SlotIndex;
	Slot.ItemInstanceId = ItemInstanceId;
	Slot.DefinitionId = DefinitionId;
	Slot.Revision = ++Bank->LastSlotRevision;
	Bank->Slots.Add(SlotIndex, Slot);
	constexpr int32 MaxAuditLogsPerGuild = 500;
	if (Bank->AuditLogs.Num() >= MaxAuditLogsPerGuild)
	{
		Bank->AuditLogs.RemoveAt(0, Bank->AuditLogs.Num() - MaxAuditLogsPerGuild + 1, EAllowShrinking::No);
	}
	Bank->AuditLogs.Add(FString::Printf(TEXT("DEPOSIT %s slot=%d item=%s"), *ActorCharId.ToString(), SlotIndex, *DefinitionId.ToString()));
	return true;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::WithdrawFromGuildBank(
	const FGuid& GuildId,
	const FGuid& ActorCharId,
	int32 SlotIndex,
	int64 ExpectedRevision,
	int32 DayKey,
	FGuid& OutItemInstanceId)
{
	return Withdraw(GuildId, ActorCharId, SlotIndex, ExpectedRevision, DayKey, OutItemInstanceId, nullptr);
}

bool UProject_JGuildAndSocialExtensionsSubsystem::WithdrawFromGuildBankToMail(
	const FGuid& GuildId, const FGuid& ActorCharId, int32 SlotIndex, int64 ExpectedRevision, int32 DayKey)
{
	auto* Economy = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
	FGuid Item;
	return Economy && Withdraw(GuildId, ActorCharId, SlotIndex, ExpectedRevision, DayKey, Item, Economy);
}

bool UProject_JGuildAndSocialExtensionsSubsystem::Withdraw(const FGuid& GuildId, const FGuid& ActorCharId,
	int32 SlotIndex, int64 ExpectedRevision, int32 DayKey, FGuid& OutItemInstanceId, UProject_JEconomySubsystem* Delivery)
{
	check(IsInGameThread());
	FGuildBankState* Bank = GuildBanks.Find(GuildId);
	if (!Bank) return false;
	const EProject_JGuildRank* Rank = Bank->MemberRanks.Find(ActorCharId);
	if (!Rank) return false;

	const int32 Limit = GetDailyWithdrawLimit(*Rank);
	if (Limit <= 0)
	{
		return false;
	}
	const auto* Daily = Bank->DailyWithdrawCounts.Find(ActorCharId);
	if (DayKey <= 0 || (Daily && DayKey < Daily->Key)) return false;
	const int32 ExistingUsed = Daily && DayKey == Daily->Key ? Daily->Value : 0;
	if (ExistingUsed >= Limit)
	{
		return false; // RBAC rank or daily withdrawal cap exceeded.
	}

	FProject_JGuildBankSlot* Slot = Bank->Slots.Find(SlotIndex);
	if (!Slot || Slot->Revision != ExpectedRevision)
	{
		return false; // Optimistic concurrency conflict prevents double-withdrawal without polluting DailyWithdrawCounts.
	}
	if (Delivery && !Delivery->SendGuildWithdrawalMail(GuildId, ActorCharId, Slot->ItemInstanceId, Slot->DefinitionId)) return false;

	OutItemInstanceId = Slot->ItemInstanceId;
	const FName DefId = Slot->DefinitionId;
	Bank->Slots.Remove(SlotIndex);
	Bank->DailyWithdrawCounts.Add(ActorCharId, TPair<int32, int32>(DayKey, ExistingUsed + 1));
	constexpr int32 MaxAuditLogsPerGuild = 500;
	if (Bank->AuditLogs.Num() >= MaxAuditLogsPerGuild)
	{
		Bank->AuditLogs.RemoveAt(0, Bank->AuditLogs.Num() - MaxAuditLogsPerGuild + 1, EAllowShrinking::No);
	}
	Bank->AuditLogs.Add(FString::Printf(TEXT("WITHDRAW %s slot=%d item=%s"), *ActorCharId.ToString(), SlotIndex, *DefId.ToString()));
	return true;
}

int32 UProject_JGuildAndSocialExtensionsSubsystem::GetGuildBankAuditLogCount(const FGuid& GuildId) const
{
	const FGuildBankState* Bank = GuildBanks.Find(GuildId);
	return Bank ? Bank->AuditLogs.Num() : 0;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::SendFriendRequest(const FGuid& FromCharId, const FGuid& TargetCharId)
{
	if (!FromCharId.IsValid() || !TargetCharId.IsValid() || FromCharId == TargetCharId
		|| IsBlockedBy(TargetCharId, FromCharId) || IsBlockedBy(FromCharId, TargetCharId))
	{
		return false;
	}
	PendingFriendRequests.FindOrAdd(TargetCharId).Add(FromCharId);
	return true;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::AcceptFriendRequest(const FGuid& AccepterCharId, const FGuid& RequesterCharId)
{
	TSet<FGuid>* Pending = PendingFriendRequests.Find(AccepterCharId);
	if (!Pending || Pending->Remove(RequesterCharId) == 0 || IsBlockedBy(AccepterCharId, RequesterCharId)
		|| IsBlockedBy(RequesterCharId, AccepterCharId))
	{
		return false;
	}
	Friends.FindOrAdd(AccepterCharId).Add(RequesterCharId);
	Friends.FindOrAdd(RequesterCharId).Add(AccepterCharId);
	return true;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::AreFriends(const FGuid& CharA, const FGuid& CharB) const
{
	const TSet<FGuid>* SetA = Friends.Find(CharA);
	return SetA && SetA->Contains(CharB);
}

bool UProject_JGuildAndSocialExtensionsSubsystem::BlockCharacter(const FGuid& OwnerCharId, const FGuid& BlockedCharId)
{
	if (!OwnerCharId.IsValid() || !BlockedCharId.IsValid() || OwnerCharId == BlockedCharId)
	{
		return false;
	}
	BlockedLists.FindOrAdd(OwnerCharId).Add(BlockedCharId);
	if (TSet<FGuid>* F1 = Friends.Find(OwnerCharId)) F1->Remove(BlockedCharId);
	if (TSet<FGuid>* F2 = Friends.Find(BlockedCharId)) F2->Remove(OwnerCharId);
	if (TSet<FGuid>* Pend = PendingFriendRequests.Find(OwnerCharId)) Pend->Remove(BlockedCharId);
	if (TSet<FGuid>* Pend = PendingFriendRequests.Find(BlockedCharId)) Pend->Remove(OwnerCharId);
	return true;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::UnblockCharacter(const FGuid& OwnerCharId, const FGuid& BlockedCharId)
{
	TSet<FGuid>* Set = BlockedLists.Find(OwnerCharId);
	return Set && Set->Remove(BlockedCharId) > 0;
}

bool UProject_JGuildAndSocialExtensionsSubsystem::IsBlockedBy(const FGuid& ReceiverCharId, const FGuid& SenderCharId) const
{
	const TSet<FGuid>* Set = BlockedLists.Find(ReceiverCharId);
	return Set && Set->Contains(SenderCharId);
}

bool UProject_JGuildAndSocialExtensionsSubsystem::AdmitChatMessage(
	const FGuid& SenderCharId,
	const FGuid& OptionalRecipientCharId,
	EProject_JChatChannel Channel,
	const FString& Message,
	double NowSeconds)
{
	if (!SenderCharId.IsValid() || !FMath::IsFinite(NowSeconds) || Message.IsEmpty() || Message.Len() > 512
		|| Channel > EProject_JChatChannel::World)
	{
		return false;
	}
	if (Channel == EProject_JChatChannel::Whisper && (!OptionalRecipientCharId.IsValid() || IsBlockedBy(OptionalRecipientCharId, SenderCharId)))
	{
		return false;
	}
	const double MinInterval = (Channel == EProject_JChatChannel::World) ? 10.0 : 1.0;
	const TPair<FGuid, EProject_JChatChannel> Key(SenderCharId, Channel);
	if (const double* LastTime = LastChatSendTimes.Find(Key))
	{
		if (NowSeconds >= *LastTime && (NowSeconds - *LastTime) < MinInterval)
		{
			return false;
		}
	}
	LastChatSendTimes.Add(Key, NowSeconds);
	return true;
}

void UProject_JGuildAndSocialExtensionsSubsystem::UpdatePresence(const FProject_JPresenceInfo& Info)
{
	if (Info.CharacterId.IsValid())
	{
		PresenceMap.Add(Info.CharacterId, Info);
	}
}

bool UProject_JGuildAndSocialExtensionsSubsystem::GetPresence(const FGuid& CharacterId, FProject_JPresenceInfo& OutInfo) const
{
	if (const FProject_JPresenceInfo* Found = PresenceMap.Find(CharacterId))
	{
		OutInfo = *Found;
		return true;
	}
	return false;
}
