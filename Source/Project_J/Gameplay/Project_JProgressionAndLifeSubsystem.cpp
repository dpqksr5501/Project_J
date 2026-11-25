#include "Gameplay/Project_JProgressionAndLifeSubsystem.h"

void UProject_JProgressionAndLifeSubsystem::Deinitialize()
{
	RepeatingQuestClaims.Reset();
	PersonalNodeCooldowns.Reset();
	CompetitiveNodeCooldowns.Reset();
	Super::Deinitialize();
}

int64 UProject_JProgressionAndLifeSubsystem::GetXPRequiredForLevel(int32 Level)
{
	const int32 Clamped = FMath::Max(1, Level);
	return static_cast<int64>(100.0 * FMath::Pow(static_cast<double>(Clamped), 2.15));
}

int32 UProject_JProgressionAndLifeSubsystem::ComputeLevelFromTotalXP(int64 TotalXP, int32 MaxLevel)
{
	int32 Level = 1;
	int64 Remaining = FMath::Max<int64>(0, TotalXP);
	while (Level < MaxLevel)
	{
		const int64 Needed = GetXPRequiredForLevel(Level);
		if (Remaining < Needed)
		{
			break;
		}
		Remaining -= Needed;
		++Level;
	}
	return Level;
}

float UProject_JProgressionAndLifeSubsystem::ComputeStatMultiplierForLevel(int32 Level, float GrowthPerLevel)
{
	return 1.0f + static_cast<float>(FMath::Max(0, Level - 1)) * FMath::Max(0.0f, GrowthPerLevel);
}

FProject_JEnhanceResult UProject_JProgressionAndLifeSubsystem::EvaluateEnhancement(
	int32 CurrentLevel,
	int32 CurrentPity,
	float ServerRollZeroToOne)
{
	FProject_JEnhanceResult Res;
	const int32 SafeCurrent = FMath::Max(0, CurrentLevel);
	const int32 SafePity = FMath::Max(0, CurrentPity);

	if (SafeCurrent < SafeEnhanceCeiling)
	{
		// Safe zone (+0 -> +7): 100% success.
		Res.bSucceeded = true;
		Res.NewEnhanceLevel = SafeCurrent + 1;
		Res.NewPityCounter = 0;
		return Res;
	}

	if (SafePity >= EnhancePityThreshold)
	{
		Res.bSucceeded = true;
		Res.bTriggeredPity = true;
		Res.NewEnhanceLevel = SafeCurrent + 1;
		Res.NewPityCounter = 0;
		return Res;
	}

	const float SuccessChance = FMath::Clamp(0.65f - 0.06f * static_cast<float>(SafeCurrent - SafeEnhanceCeiling), 0.05f, 0.65f);
	if (FMath::Clamp(ServerRollZeroToOne, 0.0f, 1.0f) <= SuccessChance)
	{
		Res.bSucceeded = true;
		Res.NewEnhanceLevel = SafeCurrent + 1;
		Res.NewPityCounter = 0;
	}
	else
	{
		Res.bSucceeded = false;
		Res.NewEnhanceLevel = SafeCurrent; // No destructive downgrade below safe ceiling; increments pity.
		Res.NewPityCounter = SafePity + 1;
	}
	return Res;
}

TArray<FProject_JSetBonusTier> UProject_JProgressionAndLifeSubsystem::EvaluateEquippedSetBonuses(const TArray<FName>& EquippedSetIds)
{
	TMap<FName, int32> Counts;
	for (const FName SetId : EquippedSetIds)
	{
		if (!SetId.IsNone())
		{
			++Counts.FindOrAdd(SetId);
		}
	}
	TArray<FProject_JSetBonusTier> Out;
	for (const auto& Pair : Counts)
	{
		FProject_JSetBonusTier Tier;
		Tier.SetId = Pair.Key;
		Tier.EquippedPieceCount = Pair.Value;
		Tier.bTwoPieceActive = Pair.Value >= 2;
		Tier.bFourPieceActive = Pair.Value >= 4;
		Out.Add(Tier);
	}
	return Out;
}

FString UProject_JProgressionAndLifeSubsystem::ComputeUtcPeriodKey(const FDateTime& UtcNow, EProject_JQuestResetPolicy Policy)
{
	if (Policy == EProject_JQuestResetPolicy::DailyUtc00)
	{
		return FString::Printf(TEXT("%04d-%02d-%02d"), UtcNow.GetYear(), UtcNow.GetMonth(), UtcNow.GetDay());
	}
	if (Policy == EProject_JQuestResetPolicy::WeeklyMonUtc00)
	{
		// UE EDayOfWeek: Monday == 0 .. Sunday == 6. Compute exact Monday 00:00 UTC so year boundaries never split a week.
		const int32 DaysSinceMonday = static_cast<int32>(UtcNow.GetDayOfWeek());
		const FDateTime WeekMonday = FDateTime(UtcNow.GetYear(), UtcNow.GetMonth(), UtcNow.GetDay()) - FTimespan::FromDays(DaysSinceMonday);
		return FString::Printf(TEXT("W-%04d-%02d-%02d"), WeekMonday.GetYear(), WeekMonday.GetMonth(), WeekMonday.GetDay());
	}
	return TEXT("ONCE");
}

bool UProject_JProgressionAndLifeSubsystem::CanAcceptChainQuest(
	const FGuid& CharacterId,
	FName QuestId,
	const TArray<FName>& Prerequisites,
	const TSet<FName>& CompletedQuests) const
{
	if (!CharacterId.IsValid() || QuestId.IsNone())
	{
		return false;
	}
	for (const FName Prereq : Prerequisites)
	{
		if (!Prereq.IsNone() && !CompletedQuests.Contains(Prereq))
		{
			return false;
		}
	}
	return true;
}

bool UProject_JProgressionAndLifeSubsystem::TryClaimRepeatingQuest(
	const FGuid& CharacterId,
	FName QuestId,
	EProject_JQuestResetPolicy Policy,
	const FDateTime& UtcNow)
{
	if (!CharacterId.IsValid() || QuestId.IsNone())
	{
		return false;
	}
	const FString PeriodKey = ComputeUtcPeriodKey(UtcNow, Policy);
	const FString ClaimKey = FString::Printf(TEXT("%s:%s:%s"),
		*CharacterId.ToString(EGuidFormats::Digits), *QuestId.ToString(), *PeriodKey);
	if (RepeatingQuestClaims.Contains(ClaimKey))
	{
		return false;
	}
	RepeatingQuestClaims.Add(ClaimKey);
	return true;
}

bool UProject_JProgressionAndLifeSubsystem::TryGatherResourceNode(
	const FGuid& NodeId,
	EProject_JResourceNodeMode Mode,
	const FGuid& CharacterId,
	int32 PlayerToolTier,
	int32 RequiredToolTier,
	double RespawnSeconds,
	double NowSeconds)
{
	if (!NodeId.IsValid() || !CharacterId.IsValid() || PlayerToolTier < RequiredToolTier
		|| !FMath::IsFinite(RespawnSeconds) || RespawnSeconds <= 0.0 || !FMath::IsFinite(NowSeconds))
	{
		return false;
	}

	if (Mode == EProject_JResourceNodeMode::PersonalPhase)
	{
		const TPair<FGuid, FGuid> Key(NodeId, CharacterId);
		if (const double* ReadyAt = PersonalNodeCooldowns.Find(Key); ReadyAt && NowSeconds < *ReadyAt)
		{
			return false;
		}
		PersonalNodeCooldowns.Add(Key, NowSeconds + RespawnSeconds);
		return true;
	}

	if (const double* ReadyAt = CompetitiveNodeCooldowns.Find(NodeId); ReadyAt && NowSeconds < *ReadyAt)
	{
		return false;
	}
	CompetitiveNodeCooldowns.Add(NodeId, NowSeconds + RespawnSeconds);
	return true;
}

float UProject_JProgressionAndLifeSubsystem::ComputeGreatSuccessChance(int32 PlayerProficiency, int32 RecipeDifficulty)
{
	const int32 Advantage = PlayerProficiency - RecipeDifficulty;
	return FMath::Clamp(0.05f + static_cast<float>(Advantage) * 0.01f, 0.02f, 0.35f);
}
