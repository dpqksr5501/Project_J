#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassEntityTypes.h"
#include "Project_JMassMonster_Trait.generated.h"

/**
 * A basic Mass Fragment that holds simple Monster stats (e.g., Health, MoveSpeed)
 * used in Data-Oriented processing.
 */
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JMassMonsterStatsFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Stats")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Stats")
	float CurrentHealth = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Stats")
	float MoveSpeed = 300.0f;
};

/**
 * Trait to add Project_J specific Monster fragments to a Mass Entity.
 * Can be added to a MassSpawner asset in the editor.
 */
UCLASS(meta=(DisplayName="Project J Monster Stats Trait"))
class PROJECT_JCHARACTER_API UProject_JMassMonster_Trait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	// Default stats applied to all entities spawned with this trait
	UPROPERTY(EditAnywhere, Category = "Project J")
	FProject_JMassMonsterStatsFragment DefaultStats;
};
