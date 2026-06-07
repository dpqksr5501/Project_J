#include "Project_JMMOTypes.h"

FProject_JAccountId FProject_JAccountId::NewId()
{
	FProject_JAccountId NewAccountId;
	NewAccountId.Value = FGuid::NewGuid();
	return NewAccountId;
}

FProject_JCharacterId FProject_JCharacterId::NewId()
{
	FProject_JCharacterId NewCharacterId;
	NewCharacterId.Value = FGuid::NewGuid();
	return NewCharacterId;
}

FProject_JRequestId FProject_JRequestId::NewId()
{
	FProject_JRequestId NewRequestId;
	NewRequestId.Value = FGuid::NewGuid();
	return NewRequestId;
}

FProject_JIdempotencyKey FProject_JIdempotencyKey::NewKey()
{
	FProject_JIdempotencyKey NewIdempotencyKey;
	NewIdempotencyKey.Value = FGuid::NewGuid();
	return NewIdempotencyKey;
}

FProject_JTransactionId FProject_JTransactionId::NewId()
{
	FProject_JTransactionId NewTransactionId;
	NewTransactionId.Value = FGuid::NewGuid();
	return NewTransactionId;
}

FProject_JItemInstanceId FProject_JItemInstanceId::NewId()
{
	FProject_JItemInstanceId NewItemInstanceId;
	NewItemInstanceId.Value = FGuid::NewGuid();
	return NewItemInstanceId;
}

FString FProject_JWorldInstanceId::ToDebugString() const
{
	return FString::Printf(
		TEXT("World=%s Zone=%s Instance=%s Channel=%s"),
		*WorldId.ToString(),
		*ZoneId.ToString(),
		*InstanceId.ToString(),
		*ChannelId.ToString());
}

void FProject_JReplicationPolicyDecision::AddReason(EProject_JReplicationRelevanceReason Reason)
{
	RelevanceReasonMask |= static_cast<int32>(Reason);
}

bool FProject_JReplicationPolicyDecision::HasReason(EProject_JReplicationRelevanceReason Reason) const
{
	return (RelevanceReasonMask & static_cast<int32>(Reason)) != 0;
}

FString FProject_JReplicationPolicyDecision::ToDebugString() const
{
	TArray<FString> Reasons;
	if (HasReason(EProject_JReplicationRelevanceReason::Distance))
	{
		Reasons.Add(TEXT("Distance"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::Owner))
	{
		Reasons.Add(TEXT("Owner"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::Party))
	{
		Reasons.Add(TEXT("Party"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::Guild))
	{
		Reasons.Add(TEXT("Guild"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::Combat))
	{
		Reasons.Add(TEXT("Combat"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::PublicEvent))
	{
		Reasons.Add(TEXT("PublicEvent"));
	}
	if (HasReason(EProject_JReplicationRelevanceReason::AlwaysRelevant))
	{
		Reasons.Add(TEXT("AlwaysRelevant"));
	}

	return FString::Printf(
		TEXT("Replicate=%s DistanceSq=%.0f Priority=%.2f Reasons=%s"),
		bShouldReplicate ? TEXT("true") : TEXT("false"),
		DistanceSquared,
		PriorityMultiplier,
		Reasons.Num() > 0 ? *FString::Join(Reasons, TEXT("|")) : TEXT("None"));
}

float FProject_JReplicationPolicySettings::GetMaxReplicationDistanceSquared() const
{
	return FMath::Square(MaxReplicationDistance);
}
