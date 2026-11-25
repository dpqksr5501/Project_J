#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JProgressionAndLifeSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProject_JQuestResetPolicy : uint8
{
	None,
	DailyUtc00,
	WeeklyMonUtc00
};

UENUM(BlueprintType)
enum class EProject_JResourceNodeMode : uint8
{
	/** Default per BP05: each player has an independent personal gather cooldown on the node. */
	PersonalPhase,
	/** Rare nodes: single winner consumes node and triggers shared world respawn. */
	CompetitiveShared
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JEnhanceResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Enhance")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Enhance")
	bool bTriggeredPity = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Enhance")
	int32 NewEnhanceLevel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Enhance")
	int32 NewPityCounter = 0;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JSetBonusTier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Set")
	FName SetId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Set")
	int32 EquippedPieceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Set")
	bool bTwoPieceActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression|Set")
	bool bFourPieceActive = false;
};

/**
 * Progression, Gear Enhancement/Sets, Chain & Repeating UTC Quests, Gathering & Crafting Rules (BP05).
 */
UCLASS()
class PROJECT_J_API UProject_JProgressionAndLifeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SafeEnhanceCeiling = 7;
	static constexpr int32 EnhancePityThreshold = 10;

	virtual void Deinitialize() override;

	// --- Leveling & Stat Scaling ---
	static int64 GetXPRequiredForLevel(int32 Level);
	static int32 ComputeLevelFromTotalXP(int64 TotalXP, int32 MaxLevel = 60);
	static float ComputeStatMultiplierForLevel(int32 Level, float GrowthPerLevel = 0.08f);

	// --- Equipment Enhancement (Safe-Zone + Pity) & Set Bonuses ---
	static FProject_JEnhanceResult EvaluateEnhancement(int32 CurrentLevel, int32 CurrentPity, float ServerRollZeroToOne);
	static TArray<FProject_JSetBonusTier> EvaluateEquippedSetBonuses(const TArray<FName>& EquippedSetIds);

	// --- Chain & UTC Repeating Quest Claims (complements Format4 history pages) ---
	static FString ComputeUtcPeriodKey(const FDateTime& UtcNow, EProject_JQuestResetPolicy Policy);
	bool CanAcceptChainQuest(const FGuid& CharacterId, FName QuestId, const TArray<FName>& Prerequisites, const TSet<FName>& CompletedQuests) const;
	bool TryClaimRepeatingQuest(const FGuid& CharacterId, FName QuestId, EProject_JQuestResetPolicy Policy, const FDateTime& UtcNow);

	// --- Gathering Nodes (Personal-Phase vs Competitive) ---
	bool TryGatherResourceNode(
		const FGuid& NodeId,
		EProject_JResourceNodeMode Mode,
		const FGuid& CharacterId,
		int32 PlayerToolTier,
		int32 RequiredToolTier,
		double RespawnSeconds,
		double NowSeconds);

	// --- Crafting Proficiency & Great Success ---
	static float ComputeGreatSuccessChance(int32 PlayerProficiency, int32 RecipeDifficulty);

private:
	TSet<FString> RepeatingQuestClaims; // char:quest:period_key
	TMap<TPair<FGuid, FGuid>, double> PersonalNodeCooldowns; // (NodeId, CharId) -> ReadyAt
	TMap<FGuid, double> CompetitiveNodeCooldowns; // NodeId -> ReadyAt
};
