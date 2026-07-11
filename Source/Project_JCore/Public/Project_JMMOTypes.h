#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JAccountId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Identity")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JAccountId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JCharacterId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Identity")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JCharacterId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JRequestId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Backend")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JRequestId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JIdempotencyKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Backend")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JIdempotencyKey NewKey();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JTransactionId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Backend")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JTransactionId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JItemInstanceId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Inventory")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JItemInstanceId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JWorldInstanceId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName WorldId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName InstanceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName ChannelId = NAME_None;

	bool IsValid() const { return !WorldId.IsNone() && !ZoneId.IsNone(); }
	FString ToDebugString() const;
};

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EProject_JReplicationRelevanceReason : uint8
{
	None = 0 UMETA(Hidden),
	Distance = 1 << 0,
	Owner = 1 << 1,
	Party = 1 << 2,
	Guild = 1 << 3,
	Combat = 1 << 4,
	PublicEvent = 1 << 5,
	AlwaysRelevant = 1 << 6
};
ENUM_CLASS_FLAGS(EProject_JReplicationRelevanceReason)

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JReplicationPolicyDecision
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Replication")
	bool bShouldReplicate = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Replication", meta = (Bitmask, BitmaskEnum = "/Script/Project_JCore.EProject_JReplicationRelevanceReason"))
	int32 RelevanceReasonMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Replication")
	float DistanceSquared = TNumericLimits<float>::Max();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Replication")
	float PriorityMultiplier = 1.0f;

	void AddReason(EProject_JReplicationRelevanceReason Reason);
	bool HasReason(EProject_JReplicationRelevanceReason Reason) const;
	FString ToDebugString() const;
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JReplicationPolicySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Replication", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxReplicationDistance = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Replication")
	bool bAlwaysReplicateOwnerOrInstigator = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Replication", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float PartyPriorityMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Replication", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float GuildPriorityMultiplier = 1.25f;

	float GetMaxReplicationDistanceSquared() const;
	float GetPartyPriorityMultiplier() const;
	float GetGuildPriorityMultiplier() const;
};
