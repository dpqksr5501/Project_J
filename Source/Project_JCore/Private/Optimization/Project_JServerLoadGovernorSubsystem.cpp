#include "Optimization/Project_JServerLoadGovernorSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Security/Project_JServerIntegritySubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJServerGovernor, Log, All);

namespace
{
FAutoConsoleCommandWithWorld GProjectJGovernorStatusCmd(
	TEXT("ProjectJ.Server.GovernorStatus"),
	TEXT("Logs current Adaptive Server Load Governor tier, tick percentiles, and active caps."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const auto* Gov = GI ? GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>() : nullptr;
		if (!Gov)
		{
			UE_LOG(LogProjectJServerGovernor, Warning, TEXT("GovernorStatus: no active subsystem."));
			return;
		}
		const FProject_JServerGovernorSnapshot Snap = Gov->GetSnapshot();
		UE_LOG(LogProjectJServerGovernor, Display,
			TEXT("ServerGovernor tier=%d ccu=%d last=%.2fms ewma=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms over33=%lld transitions=%lld crowdScaleCap60=%d npcScale=%.2f"),
			static_cast<int32>(Snap.Tier), Snap.ActiveCCU, Snap.LastTickMs, Snap.EwmaTickMs,
			Snap.TickP50Ms, Snap.TickP95Ms, Snap.TickP99Ms, Snap.OverBudgetTicksTotal,
			Snap.TierTransitionsTotal, Gov->GetEffectiveCrowdCap(60), Gov->GetNpcDecisionCadenceScale());
	}));

FAutoConsoleCommandWithWorldAndArgs GProjectJGovernorForceTierCmd(
	TEXT("ProjectJ.Server.ForceLoadTier"),
	TEXT("Overrides server load governor tier for testing/drills. Usage: ProjectJ.Server.ForceLoadTier <-1|0|1|2>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		auto* Gov = GI ? GI->GetSubsystem<UProject_JServerLoadGovernorSubsystem>() : nullptr;
		int32 Value = -1;
		if (!Gov || Args.Num() != 1 || !LexTryParseString(Value, *Args[0]) || Value < -1 || Value > 2)
		{
			UE_LOG(LogProjectJServerGovernor, Warning, TEXT("Usage: ProjectJ.Server.ForceLoadTier <-1|0|1|2>"));
			return;
		}
		Gov->SetForcedTierOverride(Value);
		UE_LOG(LogProjectJServerGovernor, Display, TEXT("ServerGovernor forced tier override set to %d"), Value);
	}));
}

void UProject_JServerLoadGovernorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickRing.SetNumZeroed(SampleWindowSize);
	TickRingHead = 0;
	TickRingCount = 0;
	TickIntervalHistogram = {};
	TickStartHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::OnWorldTickStart);
	TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::OnWorldTickEnd);
}

void UProject_JServerLoadGovernorSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickStart.Remove(TickStartHandle);
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	SampledWorld.Reset();
	TickIntervalWorld.Reset();
	LastWorldTickStartSeconds = 0.0;
	SampledTickIntervalSeconds = -1.0;
	TickRing.Reset();
	TickRingHead = 0;
	TickRingCount = 0;
	Super::Deinitialize();
}

void UProject_JServerLoadGovernorSubsystem::OnWorldTickStart(UWorld* World, ELevelTick TickType, float)
{
	if (TickType != LEVELTICK_All || !IsValid(World) || World->bIsTearingDown
		|| !GetGameInstance() || World != GetGameInstance()->GetWorld()) return;
	const ENetMode NetMode = World->GetNetMode();
	if (NetMode != NM_DedicatedServer && NetMode != NM_ListenServer) return;
	SampledWorld = World;
	TickStartedAt = FPlatformTime::Seconds();
	SampledTickIntervalSeconds = TickIntervalWorld.Get() == World && LastWorldTickStartSeconds > 0.0
		? TickStartedAt - LastWorldTickStartSeconds : -1.0;
	TickIntervalWorld = World;
	LastWorldTickStartSeconds = TickStartedAt;
}

void UProject_JServerLoadGovernorSubsystem::OnWorldTickEnd(UWorld* World, ELevelTick, float)
{
	if (!World || SampledWorld.Get() != World) return;
	SampledWorld.Reset();
	if (World->bIsTearingDown) return;
	const double Now = FPlatformTime::Seconds();
	const AGameStateBase* GS = World->GetGameState();
	RecordTickSample((Now - TickStartedAt) * 1000., GS ? GS->PlayerArray.Num() : 0, Now, SampledTickIntervalSeconds);
}

void UProject_JServerLoadGovernorSubsystem::RecordTickSample(double TickDurationMs, int32 CurrentCCU, double NowSeconds,
	double TickIntervalSeconds)
{
	check(IsInGameThread());
	if (!FMath::IsFinite(TickDurationMs) || TickDurationMs < 0.0 || !FMath::IsFinite(NowSeconds))
	{
		return;
	}

	if (TickIntervalSeconds >= 0.0 && FMath::IsFinite(TickIntervalSeconds))
	{
		++TickIntervalHistogram.Count;
		TickIntervalHistogram.SumSeconds += TickIntervalSeconds;
		for (int32 Index = 0; Index < FProject_JTickIntervalHistogram::BucketCount; ++Index)
		{
			if (TickIntervalSeconds > FProject_JTickIntervalHistogram::BoundsSeconds[Index]) continue;
			++TickIntervalHistogram.BucketCounts[Index];
			break;
		}
	}

	LastTickMs = TickDurationMs;
	LastCCU = FMath::Max(0, CurrentCCU);
	if (TickRingCount == 0)
	{
		EwmaTickMs = TickDurationMs;
	}
	else
	{
		EwmaTickMs = EwmaTickMs * 0.85 + TickDurationMs * 0.15;
	}

	if (TickDurationMs > TargetBudgetMs)
	{
		++OverBudgetTicksTotal;
	}

	if (TickRing.Num() != SampleWindowSize)
	{
		TickRing.SetNumZeroed(SampleWindowSize);
	}
	TickRing[TickRingHead] = TickDurationMs;
	TickRingHead = (TickRingHead + 1) % SampleWindowSize;
	TickRingCount = FMath::Min(TickRingCount + 1, SampleWindowSize);

	RecomputePercentiles();
	EvaluateTierTransitions(NowSeconds);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (auto* Integrity = GI->GetSubsystem<UProject_JServerIntegritySubsystem>())
		{
			Integrity->UpdateTelemetryCounters(LastTickMs, LastCCU);
		}
	}
}

void UProject_JServerLoadGovernorSubsystem::RecomputePercentiles()
{
	if (TickRingCount <= 0)
	{
		CachedP50Ms = CachedP95Ms = CachedP99Ms = 0.0;
		return;
	}
	TArray<double, TInlineAllocator<SampleWindowSize>> Sorted;
	Sorted.Append(TickRing.GetData(), TickRingCount);
	Sorted.Sort();

	const auto Quantile = [&Sorted](double Q) -> double
	{
		const int32 Idx = FMath::Clamp(FMath::CeilToInt(Q * static_cast<double>(Sorted.Num())) - 1, 0, Sorted.Num() - 1);
		return Sorted[Idx];
	};
	CachedP50Ms = Quantile(0.50);
	CachedP95Ms = Quantile(0.95);
	CachedP99Ms = Quantile(0.99);
}

void UProject_JServerLoadGovernorSubsystem::EvaluateTierTransitions(double NowSeconds)
{
	// Use p95 as primary stability signal to ignore single-tick GC/OS jitter.
	const double MetricMs = CachedP95Ms;
	EProject_JServerLoadTier TargetTier = CurrentTier;

	switch (CurrentTier)
	{
	case EProject_JServerLoadTier::Green:
		if (MetricMs >= RedEnterMs)
		{
			TargetTier = EProject_JServerLoadTier::Red;
		}
		else if (MetricMs >= YellowEnterMs)
		{
			TargetTier = EProject_JServerLoadTier::Yellow;
		}
		break;
	case EProject_JServerLoadTier::Yellow:
		if (MetricMs >= RedEnterMs)
		{
			TargetTier = EProject_JServerLoadTier::Red;
		}
		else if (MetricMs <= YellowExitMs)
		{
			TargetTier = EProject_JServerLoadTier::Green;
		}
		break;
	case EProject_JServerLoadTier::Red:
		if (MetricMs <= RedExitMs)
		{
			TargetTier = (MetricMs <= YellowExitMs) ? EProject_JServerLoadTier::Green : EProject_JServerLoadTier::Yellow;
		}
		break;
	}

	if (TargetTier > CurrentTier)
	{
		RecoverCandidateSince = -1.0;
		if (EscalateCandidateSince < 0.0)
		{
			EscalateCandidateSince = NowSeconds;
		}
		if (NowSeconds - EscalateCandidateSince >= EscalateHoldSeconds)
		{
			const EProject_JServerLoadTier OldTier = CurrentTier;
			CurrentTier = TargetTier;
			EscalateCandidateSince = -1.0;
			++TierTransitionsTotal;
			UE_LOG(LogProjectJServerGovernor, Warning,
				TEXT("ServerLoadGovernor escalated %d -> %d (p95=%.2fms ewma=%.2fms ccu=%d)"),
				static_cast<int32>(OldTier), static_cast<int32>(CurrentTier), CachedP95Ms, EwmaTickMs, LastCCU);
		}
	}
	else if (TargetTier < CurrentTier)
	{
		EscalateCandidateSince = -1.0;
		if (RecoverCandidateSince < 0.0)
		{
			RecoverCandidateSince = NowSeconds;
		}
		if (NowSeconds - RecoverCandidateSince >= RecoverHoldSeconds)
		{
			const EProject_JServerLoadTier OldTier = CurrentTier;
			CurrentTier = TargetTier;
			RecoverCandidateSince = -1.0;
			++TierTransitionsTotal;
			UE_LOG(LogProjectJServerGovernor, Display,
				TEXT("ServerLoadGovernor recovered %d -> %d (p95=%.2fms ewma=%.2fms ccu=%d)"),
				static_cast<int32>(OldTier), static_cast<int32>(CurrentTier), CachedP95Ms, EwmaTickMs, LastCCU);
		}
	}
	else
	{
		EscalateCandidateSince = -1.0;
		RecoverCandidateSince = -1.0;
	}
}

void UProject_JServerLoadGovernorSubsystem::SetForcedTierOverride(int32 ForcedTierIndex)
{
	check(IsInGameThread());
	ForcedTierOverride = (ForcedTierIndex >= 0 && ForcedTierIndex <= 2) ? ForcedTierIndex : -1;
}

EProject_JServerLoadTier UProject_JServerLoadGovernorSubsystem::GetCurrentTier() const
{
	if (ForcedTierOverride >= 0 && ForcedTierOverride <= 2)
	{
		return static_cast<EProject_JServerLoadTier>(ForcedTierOverride);
	}
	return CurrentTier;
}

FProject_JServerGovernorSnapshot UProject_JServerLoadGovernorSubsystem::GetSnapshot() const
{
	FProject_JServerGovernorSnapshot Snap;
	Snap.Tier = GetCurrentTier();
	Snap.LastTickMs = LastTickMs;
	Snap.EwmaTickMs = EwmaTickMs;
	Snap.TickP50Ms = CachedP50Ms;
	Snap.TickP95Ms = CachedP95Ms;
	Snap.TickP99Ms = CachedP99Ms;
	Snap.OverBudgetTicksTotal = OverBudgetTicksTotal;
	Snap.ActiveCCU = LastCCU;
	Snap.TierTransitionsTotal = TierTransitionsTotal;
	return Snap;
}

int32 UProject_JServerLoadGovernorSubsystem::GetEffectiveCrowdCap(int32 BaseCap) const
{
	if (BaseCap <= 0)
	{
		return 0; // Uncapped explicitly requested.
	}
	switch (GetCurrentTier())
	{
	case EProject_JServerLoadTier::Yellow:
		return FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseCap) * 0.70f), FMath::Min(MinCrowdCapFloor, BaseCap), BaseCap);
	case EProject_JServerLoadTier::Red:
		return FMath::Clamp(FMath::RoundToInt(static_cast<float>(BaseCap) * 0.45f), FMath::Min(MinCrowdCapFloor, BaseCap), BaseCap);
	case EProject_JServerLoadTier::Green:
	default:
		return BaseCap;
	}
}

float UProject_JServerLoadGovernorSubsystem::GetNpcDecisionCadenceScale() const
{
	switch (GetCurrentTier())
	{
	case EProject_JServerLoadTier::Yellow: return 1.5f;
	case EProject_JServerLoadTier::Red: return 2.5f;
	case EProject_JServerLoadTier::Green:
	default: return 1.0f;
	}
}

bool UProject_JServerLoadGovernorSubsystem::ShouldThrottleNonCriticalCosmetics() const
{
	return GetCurrentTier() != EProject_JServerLoadTier::Green;
}

int32 UProject_JServerLoadGovernorSubsystem::GetCombatFeedbackMaxNearbyRecipients() const
{
	switch (GetCurrentTier())
	{
	case EProject_JServerLoadTier::Yellow: return 16;
	case EProject_JServerLoadTier::Red: return 8;
	case EProject_JServerLoadTier::Green:
	default: return 32;
	}
}
