// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JCombatAnimProfile.generated.h"

class UProject_JMotionMatchingAssetSet;

/**
 * Combat animation policy for a playable character archetype.
 *
 * This profile owns behavior switches and animation tuning values that are shared across weapons.
 * It intentionally does not require any montage or blend space assets, so it is safe to assign
 * before combat animations are authored.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCombatAnimProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Mode")
	bool bPlayIntroMontageWhenEnteringCombat = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Mode")
	bool bUseCombatRotationMode = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Mode")
	bool bInterruptCombatIntroOnHit = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CombatAimAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	bool bAllowSprintInCombat = false;

	/**
	 * Optional combat loop Motion Matching asset set used while this profile owns
	 * camera-facing combat rotation. Assign Default/Idle and Run/Sprint Cycle PSDs.
	 * Run/Sprint TurnRedirect PSDs are optional for locomotion modes that use them;
	 * authored Start, Stop, Pivot, Jump, Fall Off and Landing clips stay owned by
	 * the State Controller Chooser / direct Blend Stack path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching", meta = (ToolTip = "Assign combat Idle, Run/Sprint Cycle, and moving Run/Sprint TurnRedirect PSDs. One-shot Start, Stop, Pivot, Jump, Fall Off and Landing assets belong in the State Controller Choosers."))
	TObjectPtr<UProject_JMotionMatchingAssetSet> CombatStrafeMotionMatchingAssetSet = nullptr;

	/**
	 * Re-query the current combat locomotion PSD when a held movement input turns
	 * sharply. This is required for a camera-facing strafe set: W -> A/D normally
	 * stays in the same Cycle PSD, but must not keep a forward continuing pose.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching")
	bool bForceReselectOnStrafeInputTurn = true;

	/** Enables the local-only authored Combat-Strafe Run Pivot presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching|Pivot")
	bool bEnableCombatStrafeRunPivot = false;

	/** Minimum camera-relative input direction change that forces a fresh combat strafe pose search. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching", meta = (EditCondition = "bForceReselectOnStrafeInputTurn", ClampMin = "0.0", ClampMax = "180.0", UIMin = "15.0", UIMax = "90.0"))
	float StrafeInputTurnReselectAngle = 35.0f;

	/** Coalesces W->W+A->A input edges into one pose search instead of interrupting the same combat cycle twice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching", meta = (EditCondition = "bForceReselectOnStrafeInputTurn", ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float StrafeInputTurnReselectCooldown = 0.10f;

	/** Ignore very slow trajectory samples and retain the last valid combat-Strafe direction instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StrafeDirectionMinimumSpeed = 10.0f;

	/** Extra angular margin retained inside the previous eight-direction sector to prevent boundary flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Motion Matching", meta = (ClampMin = "0.0", ClampMax = "22.5", UIMin = "0.0", UIMax = "15.0"))
	float StrafeDirectionHysteresisDegrees = 7.5f;

	/** Require positive camera-relative forward input (W, W+A, or W+D) for combat sprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Movement", meta = (EditCondition = "bAllowSprintInCombat"))
	bool bRequireForwardInputForSprintInCombat = true;

	/** Minimum positive forward axis value required by the combat sprint direction rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Movement", meta = (EditCondition = "bAllowSprintInCombat && bRequireForwardInputForSprintInCombat", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CombatSprintForwardInputThreshold = 0.1f;
};
