#include "Security/Project_JServerIntegritySubsystem.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Project_JSocialStateInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJServerIntegrity, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarEnforceServerIntegrity(
	TEXT("ProjectJ.Security.EnforceServerIntegrity"),
	1,
	TEXT("Enables live server-authoritative movement/combat integrity checks (1 = enabled, 0 = disabled)."),
	ECVF_Default);


}

bool UProject_JServerIntegritySubsystem::IsServerIntegrityEnabled()
{
	return CVarEnforceServerIntegrity.GetValueOnAnyThread() != 0;
}

FGuid UProject_JServerIntegritySubsystem::ResolveActorCharacterGuid(const AActor* Actor)
{
	if (!IsValid(Actor)) return FGuid();
	if (const auto* Social = Cast<IProject_JSocialStateInterface>(Actor))
	{
		if (const FGuid Id = Social->GetAuthoritativeCharacterGuid(); Id.IsValid()) return Id;
	}
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const auto* Social = Cast<IProject_JSocialStateInterface>(Pawn->GetPlayerState()))
		{
			if (const FGuid Id = Social->GetAuthoritativeCharacterGuid(); Id.IsValid()) return Id;
		}
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		if (const auto* Social = Cast<IProject_JSocialStateInterface>(Controller->GetPlayerState<APlayerState>()))
		{
			if (const FGuid Id = Social->GetAuthoritativeCharacterGuid(); Id.IsValid()) return Id;
		}
	}
	// Deterministic non-zero fallback keyed by UObject UniqueID for standalone/test pawns without an online session.
	return FGuid(0x504A494E, Actor->GetUniqueID(), 0x00000001, 0x504A504C);
}

void UProject_JServerIntegritySubsystem::Deinitialize()
{
	Bridge.Reset();
	StateOwners.Reset();
	MovementWhitelistUntil.Reset();
	MovementViolations.Reset();
	CombatViolations.Reset();
	LastAbilityTimes.Reset();
	AuditEvents.Reset();
	Super::Deinitialize();
}

void UProject_JServerIntegritySubsystem::GrantMovementWhitelist(const FGuid& CharacterId, double DurationSeconds, double NowSeconds)
{
	if (CharacterId.IsValid() && FMath::IsFinite(DurationSeconds) && DurationSeconds > 0.0 && FMath::IsFinite(NowSeconds))
	{
		const double Until = NowSeconds + DurationSeconds;
		double& Existing = MovementWhitelistUntil.FindOrAdd(CharacterId);
		Existing = FMath::Max(Existing, Until);
	}
}

void UProject_JServerIntegritySubsystem::ClaimActorState(const FGuid& CharacterId, AActor* Actor)
{
	if (!CharacterId.IsValid() || !IsValid(Actor)) return;
	if (const auto* Old = StateOwners.Find(CharacterId); Old && Old->Get() != Actor) ClearCharacterState(CharacterId);
	StateOwners.Add(CharacterId, Actor);
}

void UProject_JServerIntegritySubsystem::ReleaseActorState(const FGuid& CharacterId, const AActor* Actor)
{
	if (const auto* Owner = StateOwners.Find(CharacterId); Owner && Owner->Get(/*bEvenIfPendingKill*/true) == Actor)
		ClearCharacterState(CharacterId);
}

void UProject_JServerIntegritySubsystem::ClearCharacterState(const FGuid& CharacterId)
{
	if (!CharacterId.IsValid()) return;
	StateOwners.Remove(CharacterId);
	MovementWhitelistUntil.Remove(CharacterId);
	MovementViolations.Remove(CharacterId);
	CombatViolations.Remove(CharacterId);
	for (auto It = LastAbilityTimes.CreateIterator(); It; ++It)
	{
		if (It.Key().Key == CharacterId)
		{
			It.RemoveCurrent();
		}
	}
}

void UProject_JServerIntegritySubsystem::AppendAuditEvent(
	const FGuid& CharacterId,
	FName Code,
	EProject_JIntegrityAction Action,
	int32 WindowCount,
	double NowSeconds)
{
	if (AuditEvents.Num() >= MaxAuditEvents)
	{
		AuditEvents.RemoveAt(0, 1, EAllowShrinking::No);
	}
	FProject_JIntegrityAuditEvent& Ev = AuditEvents.AddDefaulted_GetRef();
	Ev.CharacterId = CharacterId;
	Ev.ViolationCode = Code;
	Ev.Action = Action;
	Ev.WindowViolationCount = WindowCount;
	Ev.TimestampSeconds = NowSeconds;
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

	EProject_JIntegrityAction Action = EProject_JIntegrityAction::SnapBack;
	if (Win.Count >= KickThreshold)
	{
		Action = EProject_JIntegrityAction::DisconnectRecommended;
		if (Bridge && Win.Count == KickThreshold) Bridge->ReportServerViolation(CharacterId, Code, 3);
		if (Win.Count == KickThreshold) UE_LOG(LogProjectJServerIntegrity, Display, TEXT("ServerIntegrity DisconnectRecommended char=%s code=%s count=%d"),
			*CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), *Code.ToString(), Win.Count);
	}
	else if (Win.Count >= FlagThreshold)
	{
		Action = EProject_JIntegrityAction::FlagAudit;
		if (Bridge && Win.Count == FlagThreshold) Bridge->ReportServerViolation(CharacterId, Code, 2);
		if (Win.Count == FlagThreshold) UE_LOG(LogProjectJServerIntegrity, Display, TEXT("ServerIntegrity FlagAudit char=%s code=%s count=%d"),
			*CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), *Code.ToString(), Win.Count);
	}
	AppendAuditEvent(CharacterId, Code, Action, Win.Count, NowSeconds);
	return Action;
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
	if (const double* WhitelistUntil = MovementWhitelistUntil.Find(CharacterId))
	{
		if (NowSeconds <= *WhitelistUntil) return EProject_JIntegrityAction::Accept;
		MovementWhitelistUntil.Remove(CharacterId);
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
	if (!CharacterId.IsValid() || AbilityId.IsNone() || !FMath::IsFinite(MinIntervalSeconds) || MinIntervalSeconds <= 0.0 || !FMath::IsFinite(NowSeconds))
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
	AppendAuditEvent(AccountId, AnomalyCode, EProject_JIntegrityAction::FlagAudit, 1, FPlatformTime::Seconds());
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
