// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JCombatAnimProfile.generated.h"

/**
 * Combat animation policy for a playable character archetype.
 *
 * This profile owns behavior switches and animation tuning values that are shared across weapons.
 * It intentionally does not require any montage or blend space assets, so it is safe to assign
 * before combat animations are authored.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCombatAnimProfile : public UDataAsset
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
};
