#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "Project_JMassMonsterSpawner.generated.h"

/**
 * A custom C++ Mass Spawner specifically configured to spawn monsters or NPCs.
 * Instead of setting up everything in Blueprints, you can pre-configure default properties here.
 */
UCLASS()
class PROJECT_JCHARACTER_API AProject_JMassMonsterSpawner : public AMassSpawner
{
	GENERATED_BODY()

public:
	AProject_JMassMonsterSpawner();

protected:
	virtual void BeginPlay() override;
};
