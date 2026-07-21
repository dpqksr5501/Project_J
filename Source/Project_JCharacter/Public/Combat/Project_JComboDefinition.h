// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JComboDefinition.generated.h"

class UProject_JAttackDefinition;

/** A directed input edge between two authored combo nodes. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JComboTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	FGameplayTag TargetNodeTag;

	/** All of these tags must be on the owner when this input is consumed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer RequiredOwnerTags;

	/** Any of these tags prevents this edge from being used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer BlockedOwnerTags;
};

/** One playable attack in a weapon combo graph. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JComboNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag NodeTag;

	/** Inputs that can start this node while no combo is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTagContainer StartInputTags;

	/** Reusable attack payload. Combo nodes only define graph flow and conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	TObjectPtr<UProject_JAttackDefinition> AttackDefinition = nullptr;

	/** Allows one valid next input to be held before the Combo Window notify opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bAllowInputBuffer = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer RequiredOwnerTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer BlockedOwnerTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	TArray<FProject_JComboTransition> Transitions;
};

/**
 * Immutable, weapon/job-authored combo graph. Runtime combo state deliberately
 * lives in the Gameplay Ability, so this asset can be shared by all players.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JComboDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	TArray<FProject_JComboNode> Nodes;

	const FProject_JComboNode* FindNode(FGameplayTag NodeTag) const;
	const FProject_JComboNode* FindStartNode(FGameplayTag InputTag, const FGameplayTagContainer& OwnerTags) const;
	const FProject_JComboTransition* FindTransition(const FProject_JComboNode& FromNode, FGameplayTag InputTag, const FGameplayTagContainer& OwnerTags) const;
	void GetReferencedInputTags(FGameplayTagContainer& OutInputTags) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
