#include "Security/Project_JServerIntegritySubsystem.h"

void UProject_JServerIntegritySubsystem::Deinitialize()
{
	Bridge.Reset();
	MovementWhitelistUntil.Reset();
	MovementViolations.Reset();
	CombatViolations.Reset();
	LastAbilityTimes.Reset();
	Super::Deinitialize();
}

void UProject_JServerIntegritySubsystem::GrantMovementWhitelist(const FGuid& CharacterId, double DurationSeconds, double NowSeconds)
{
	if (CharacterId.IsValid() && DurationSeconds > 0.0)
	{
		MovementWhitelistUntil.Add(CharacterId, NowSeconds + DurationSeconds);
	}
}

EProject_JIntegrityAction UProject_JServerIntegritySubsystem::RecordViolation(
	const FGuid& CharacterId,
	FName Code,
	double NowSeconds,
	TMap<FGuid, FViolationWindow>& Table,
	int64& MetricCounter)
{
	++MetricCounter;
	FViolationWindow& Win = Table.FindOrAdd(CharacterId);
	if (NowSeconds - Win.WindowStart > 10.0 || NowSeconds < Win.WindowStart)
	{
		Win.WindowStart = NowSeconds;
		Win.Count = 0;
	}
	++Win.Count;

	if (Win.Count >= KickThreshold)
	{
		if (Bridge) Bridge->ReportServerViolation(CharacterId, Code, 3);
		return EProject_JIntegrityAction::DisconnectRecommended;
	}
	if (Win.Count >= FlagThreshold)
	{
		if (Bridge) Bridge->ReportServerViolation(CharacterId, Code, 2);
		return EProject_JIntegrityAction::FlagAudit;
	}
	return EProject_JIntegrityAction::SnapBack;
}

EProject_JIntegrityAction UProject_JServerIntegritySubsystem::ValidateMovementStep(
	const FGuid& CharacterId,
	const FVector& PreviousLocation,
	const FVector& CurrentLocation,
	float MaxAllowedSpeedCmPerSec,
	double DeltaSeconds,
	double NowSeconds,
	float KillZ)
{
	if (!CharacterId.IsValid() || PreviousLocation.ContainsNaN() || CurrentLocation.ContainsNaN()
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0
		|| !FMath::IsFinite(MaxAllowedSpeedCmPerSec) || !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(KillZ))
	{
		return EProject_JIntegrityAction::SnapBack;
	}
	if (CurrentLocation.Z < KillZ)
	{
		return RecordViolation(CharacterId, TEXT("BelowKillZ"), NowSeconds, MovementViolations, Telemetry.MovementViolations);
	}
	if (const double* WhitelistUntil = MovementWhitelistUntil.Find(CharacterId); WhitelistUntil && NowSeconds <= *WhitelistUntil)
	{
		return EProject_JIntegrityAction::Accept;
	}

	const double AllowedDistance = static_cast<double>(FMath::Max(100.0f, MaxAllowedSpeedCmPerSec)) * DeltaSeconds * SpeedToleranceMultiplier + 35.0;
	const double ActualDistance = FVector::Dist2D(PreviousLocation, CurrentLocation);
	if (ActualDistance > AllowedDistance)
	{
		return RecordViolation(CharacterId, TEXT("SpeedEnvelopeExceeded"), NowSeconds, MovementViolations, Telemetry.MovementViolations);
	}
	return EProject_JIntegrityAction::Accept;
}

EProject_JIntegrityAction UProject_JServerIntegritySubsystem::ValidateCombatCadence(
	const FGuid& CharacterId,
	FName AbilityId,
	double MinIntervalSeconds,
	double NowSeconds)
{
	if (!CharacterId.IsValid() || AbilityId.IsNone() || MinIntervalSeconds <= 0.0)
	{
		return EProject_JIntegrityAction::Accept;
	}
	const TPair<FGuid, FName> Key(CharacterId, AbilityId);
	if (const double* LastTime = LastAbilityTimes.Find(Key))
	{
		if (NowSeconds - *LastTime < MinIntervalSeconds * 0.92) // 8% jitter tolerance
		{
			return RecordViolation(CharacterId, TEXT("CombatCadenceViolation"), NowSeconds, CombatViolations, Telemetry.CombatViolations);
		}
	}
	LastAbilityTimes.Add(Key, NowSeconds);
	return EProject_JIntegrityAction::Accept;
}

void UProject_JServerIntegritySubsystem::RecordEconomyAnomaly(const FGuid& AccountId, FName AnomalyCode)
{
	++Telemetry.EconomyAlerts;
	if (Bridge)
	{
		Bridge->ReportServerViolation(AccountId, AnomalyCode, 3);
	}
}

void UProject_JServerIntegritySubsystem::UpdateTelemetryCounters(double TickMs, int32 CurrentCCU)
{
	Telemetry.LastTickMs = TickMs;
	Telemetry.ActiveCCU = FMath::Max(0, CurrentCCU);
}
