// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatCommandSet.generated.h"

/** One resolved input press stored by the runtime command resolver. */
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JCombatCommandInputEntry
{
	GENERATED_BODY()

	FGameplayTag InputTag;
	double TimestampSeconds = 0.0;
};

/**
 * A Black-Desert-style command alias for a Gameplay Ability input tag.
 *
 * A command only decides which input intent to dispatch. Cooldown, resource
 * costs, montage policy, hit processing, and cancel rules remain owned by the
 * resulting Gameplay Ability. This lets a quick-slot input and a command
 * sequence activate the exact same ability without duplicating skill data.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatCommandDefinition
{
	GENERATED_BODY()

	/** Stable identifier used for debugging and validation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag CommandTag;

	/** Ordered button/chord tags. The final entry is the current input. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<FGameplayTag> OrderedInputSequence;

	/** Maximum time allowed between any two consecutive presses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MaxTimeBetweenInputs = 0.45f;

	/** Longer sequences win first; this resolves otherwise equal suffix matches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	int32 Priority = 0;

	/** The GAS input/event tag dispatched when this command matches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Result")
	FGameplayTag ResultInputTag;

	/** Prevents the final raw button input from also starting a normal attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Result")
	bool bConsumeMatchedInput = true;

	/** Clear old input history after a match so one sequence cannot trigger repeatedly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Result")
	bool bClearHistoryOnMatch = true;

	/** All tags must be owned by the character when the final input is pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer RequiredOwnerTags;

	/** Any owned tag prevents this command from firing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer BlockedOwnerTags;

	int32 GetInputCount() const { return OrderedInputSequence.Num(); }
};

/**
 * Immutable command table selected by a weapon profile. It is intentionally
 * separate from ComboDefinition: command recognition chooses a skill, while
 * ComboDefinition controls the attacks inside an already-active skill.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCombatCommandSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Commands", meta = (TitleProperty = "CommandTag"))
	TArray<FProject_JCombatCommandDefinition> Commands;

	const FProject_JCombatCommandDefinition* FindBestMatch(
		const TArray<FProject_JCombatCommandInputEntry>& InputHistory,
		const FGameplayTagContainer& OwnerTags) const;

	int32 GetMaximumInputCount() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
