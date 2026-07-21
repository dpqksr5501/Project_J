#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JWeaponType : uint8
{
	None UMETA(DisplayName = "None"),
	Unarmed UMETA(DisplayName = "Unarmed"),
	OneHandSword UMETA(DisplayName = "One-Hand Sword"),
	TwoHandSword UMETA(DisplayName = "Two-Hand Sword"),
	DualBlades UMETA(DisplayName = "Dual Blades"),
	Staff UMETA(DisplayName = "Staff"),
	Bow UMETA(DisplayName = "Bow")
};

/**
 * How a weapon family contributes continuous combat animation beneath action
 * montages. This is presentation-only; it must never change movement or GAS
 * authority.
 */
UENUM(BlueprintType)
enum class EProject_JCombatAnimationPresentationMode : uint8
{
	/** Keep the shared Motion Matching lower body and overlay only the armed upper body. */
	UpperBodyOverlay UMETA(DisplayName = "Upper-Body Overlay"),

	/** Replace continuous locomotion with a weapon-authored full-body pose set. */
	FullBodyLocomotion UMETA(DisplayName = "Full-Body Locomotion")
};

/** Gameplay parameters for one authored swing. Input ownership belongs to the combo graph, not this hit specification. */
USTRUCT(BlueprintType)
struct FProject_JComboHitSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage", meta = (ClampMin = "0.0"))
	float BaseDamage = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Trace", meta = (ClampMin = "0.0"))
	float TraceDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Trace", meta = (ClampMin = "0.0"))
	float TraceRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Impulse")
	float KnockbackImpulse = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Impulse")
	float LaunchImpulse = 250.0f;
};
