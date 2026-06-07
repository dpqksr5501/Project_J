#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatMovementPolicy.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatMovementPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bCombatMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bAttacking = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bDodging = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bHitReacting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bAllowSprintInCombat = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bUseCombatRotationMode = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Movement")
	bool bInterruptIntroOnHit = true;

	bool IsSprintBlocked() const;
	bool IsJumpAllowed() const;
	bool IsGroundStartAllowed() const;
	bool IsGroundStopAllowed() const;
	bool IsCombatLocomotionOverlayAllowed() const;
	bool ShouldUseCombatRotationMode() const;
	bool ShouldInterruptIntroOnHit() const;
};
