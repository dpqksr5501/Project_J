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

USTRUCT(BlueprintType)
struct FProject_JWeaponAttackSpec
{
	GENERATED_BODY()

	/** Preferred activation intent. AbilitySet entries should grant an ability with this InputTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Ability")
	FGameplayTag InputTag;

	/** Migration fallback for assets that still activate by ability classification tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Ability")
	FGameplayTag AbilityTag;

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
