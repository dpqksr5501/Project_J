#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JLootSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProject_JLootDistributionMode : uint8
{
	PersonalLoot,
	PartyDiceRoll,
	BossContributionPersonal
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JLootTableEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	FName ItemDefinitionId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1"))
	int32 MaxQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	bool bGuaranteed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1"))
	int32 ItemDefinitionVersion = 1;
};

UCLASS(BlueprintType)
class PROJECT_J_API UProject_JLootTableDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FName LootTableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FProject_JLootTableEntry> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int64 MinGold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int64 MaxGold = 0;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JLootItemStack
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	FName ItemDefinitionId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 Quantity = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 ItemDefinitionVersion = 1;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JRolledLootGrant
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	FGuid RecipientCharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FProject_JLootItemStack> Items;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	int64 Gold = 0;

	/** True when inventory capacity was full and reward routed to MAIL_ESCROW fallback. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	bool bRoutedToMailFallback = false;
};

/**
 * Server-authoritative Loot Roller, Party Roll Arbiter, and Giant Field Boss Contribution
 * Personal-Loot Distributor with idempotent receipts and Mail overflow fallback (BP05).
 */
UCLASS()
class PROJECT_J_API UProject_JLootSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double PartyRollTimeoutSeconds = 30.0;

	virtual void Deinitialize() override;

	/** Deterministically rolls loot using server FRandomStream (client cannot manipulate odds). */
	FProject_JRolledLootGrant RollPersonalLoot(
		const UProject_JLootTableDefinition* Table,
		const FGuid& RecipientCharacterId,
		int32 Seed) const;

	/**
	 * Distributes idempotent personal loot to all eligible giant-boss contributors.
	 * Duplicate calls for the same (EncounterId, SpawnRevision, CharacterId) are ignored.
	 */
	TArray<FProject_JRolledLootGrant> DistributeBossContributionLoot(
		FName EncounterId,
		int32 SpawnRevision,
		const UProject_JLootTableDefinition* Table,
		const TArray<FGuid>& EligibleCharacterIds,
		TFunctionRef<bool(const FGuid& /*CharId*/, int32 /*ItemCount*/)> HasInventorySpace,
		int32 BaseSeed);

	/** Starts a 30s party dice roll for a rare drop; returns RollId. */
	FGuid StartPartyRoll(FName ItemDefinitionId, const TArray<FGuid>& Participants, double NowSeconds);
	bool SubmitPartyRollChoice(const FGuid& RollId, const FGuid& CharacterId, int32 BidPriority, int32 RollValue);
	bool ResolvePartyRoll(const FGuid& RollId, double NowSeconds, FGuid& OutWinnerCharacterId);

private:
	struct FPartyRollSession
	{
		FName ItemDefinitionId;
		TSet<FGuid> Participants;
		TMap<FGuid, TPair<int32, int32>> Submissions; // (Priority: 2=Need, 1=Greed, 0=Pass; RollValue 1..100)
		double ExpiresAt = 0.0;
		bool bResolved = false;
	};
	static constexpr int32 MaxRetainedBossReceipts = 16384;
	TSet<FString> BossRewardReceipts;
	TArray<FString> BossRewardReceiptOrder;
	int32 BossRewardReceiptHead = 0;
	TMap<FGuid, FPartyRollSession> PartyRolls;
};
