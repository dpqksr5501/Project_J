#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JServerIntegritySubsystem.generated.h"

/**
 * Clean abstraction boundary for future commercial client anti-cheat (e.g. EAC / BattlEye in Stage C).
 * Development builds run with no external SDK dependency.
 */
class PROJECT_JCORE_API IProject_JAntiCheatBridge
{
public:
	virtual ~IProject_JAntiCheatBridge() = default;
	virtual bool IsClientAttested(const FGuid& AccountId) const = 0;
	virtual void ReportServerViolation(const FGuid& AccountId, FName ViolationCode, int32 Severity) = 0;
};

UENUM(BlueprintType)
enum class EProject_JIntegrityAction : uint8
{
	Accept,
	SnapBack,
	FlagAudit,
	DisconnectRecommended
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JRuntimeTelemetrySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	double LastTickMs = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	int32 ActiveCCU = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	int64 MovementViolations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	int64 CombatViolations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	int64 EconomyAlerts = 0;
};

/**
 * Server-authoritative Anti-Cheat & Lightweight Telemetry Hook Subsystem (BP06):
 *  - Movement speed envelope check with 1.15x tolerance + KillZ + authorized dash/portal whitelist
 *  - Combat cadence/cooldown violation counter with progressive SnapBack -> Flag -> Disconnect escalation
 *  - Zero-dependency metrics snapshot hook (ready for Stage C Prometheus/Sentry exporters)
 */
UCLASS()
class PROJECT_JCORE_API UProject_JServerIntegritySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double SpeedToleranceMultiplier = 1.15;
	static constexpr int32 FlagThreshold = 3;
	static constexpr int32 KickThreshold = 5;

	virtual void Deinitialize() override;

	/** Registers an optional external anti-cheat bridge (Stage C). */
	void SetAntiCheatBridge(TSharedPtr<IProject_JAntiCheatBridge> InBridge) { Bridge = MoveTemp(InBridge); }

	/** Grants a short-lived server movement whitelist window for authorized teleports/portals/dashes. */
	void GrantMovementWhitelist(const FGuid& CharacterId, double DurationSeconds, double NowSeconds);

	/** Validates observed displacement over DeltaSeconds against max allowed speed (incl. buffs/mount). */
	EProject_JIntegrityAction ValidateMovementStep(
		const FGuid& CharacterId,
		const FVector& PreviousLocation,
		const FVector& CurrentLocation,
		float MaxAllowedSpeedCmPerSec,
		double DeltaSeconds,
		double NowSeconds,
		float KillZ = -50000.0f);

	/** Validates minimum server cast interval between combat actions. */
	EProject_JIntegrityAction ValidateCombatCadence(
		const FGuid& CharacterId,
		FName AbilityId,
		double MinIntervalSeconds,
		double NowSeconds);

	/** Records an economy invariant anomaly (e.g. repeated idempotency abuse) and notifies bridge hook. */
	void RecordEconomyAnomaly(const FGuid& AccountId, FName AnomalyCode);

	void UpdateTelemetryCounters(double TickMs, int32 CurrentCCU);
	FProject_JRuntimeTelemetrySnapshot GetTelemetrySnapshot() const { return Telemetry; }

private:
	struct FViolationWindow
	{
		int32 Count = 0;
		double WindowStart = 0.0;
	};

	EProject_JIntegrityAction RecordViolation(const FGuid& CharacterId, FName Code, double NowSeconds, TMap<FGuid, FViolationWindow>& Table, int64& MetricCounter);

	TSharedPtr<IProject_JAntiCheatBridge> Bridge;
	TMap<FGuid, double> MovementWhitelistUntil;
	TMap<FGuid, FViolationWindow> MovementViolations;
	TMap<FGuid, FViolationWindow> CombatViolations;
	TMap<TPair<FGuid, FName>, double> LastAbilityTimes;
	FProject_JRuntimeTelemetrySnapshot Telemetry;
};
