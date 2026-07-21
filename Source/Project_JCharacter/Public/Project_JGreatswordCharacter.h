// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JGreatswordCharacter.generated.h"

/**
 * Native foundation for the greatsword job family.
 *
 * Class/advancement data, visual mesh, weapon profile, and AnimBP remain
 * authored in BP_GreatswordCharacter and Data Assets. This class only owns
 * greatsword-specific runtime extension points.
 */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API AProject_JGreatswordCharacter : public AProject_JPlayerCharacter
{
	GENERATED_BODY()

public:
	AProject_JGreatswordCharacter() = default;
};
