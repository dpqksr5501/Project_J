#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "Project_JMassMonsterSpawner.generated.h"

/**
 * Experimental integration point for future large-scale monsters and crowds.
 * Keep gameplay-critical NPCs on AProject_JNPCCharacter until a measured Mass
 * representation/LOD policy exists. Mass remains enabled; this class simply
 * prevents the experimental path from leaking into normal character code.
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
