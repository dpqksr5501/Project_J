#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "Project_JCheatManager.generated.h"

/**
 * Developer & GM testing commands for rapid Stage A–C iteration (non-Shipping only).
 * All mutating commands require server authority or Standalone execution.
 */
UCLASS()
class PROJECT_J_API UProject_JCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	/** Teleports the controlled pawn to world coordinates (X, Y, Z) safely. */
	UFUNCTION(Exec)
	void PJ_Teleport(float X, float Y, float Z);

	/** Restores Health and Mana to MaxHealth/MaxMana on authority. */
	UFUNCTION(Exec)
	void PJ_HealFull();

	/** Applies lethal damage to the controlled pawn to exercise State_Dead & Revive flows. */
	UFUNCTION(Exec)
	void PJ_KillSelf();

	/** Toggles simulated network latency and packet loss via engine Net PktLag/PktLoss CVars. */
	UFUNCTION(Exec)
	void PJ_SimulateLag(int32 LatencyMs, int32 LossPercent);

	/** Switches backend runtime mode: 0=StandaloneMock, 1=LocalDockerNakama, 2=DedicatedExternal. */
	UFUNCTION(Exec)
	void PJ_SetBackendMode(int32 ModeIndex);

	/** Logs key runtime metrics (WorldTime, Pawn location, NetMode, BackendMode). */
	UFUNCTION(Exec)
	void PJ_DumpRuntimeMetrics();

	/** Toggles the interactive MMO Test Harness UI overlay in non-Shipping builds. */
	UFUNCTION(Exec, BlueprintCallable, Category = "MMO|TestUI")
	void PJ_ToggleTestUI();

	/** Returns whether a mutating GM cheat is permitted in this build and authority context. */
	bool CanExecuteAuthorityCheat() const;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<class UProject_JMMOTestHarnessWidget> ActiveTestHarness;
};
