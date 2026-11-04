// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JSpellbladeCharacter.generated.h"

/**
 * Behavior-free compatibility type for native test/network fixtures.
 * Production BP_Spellblade inherits BP_Player_Master / AProject_JPlayerCharacter;
 * job, equipment and body differences are authored in existing profiles.
 * Retain this type for its native subclasses and serialized fixture consumers.
 */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API AProject_JSpellbladeCharacter : public AProject_JPlayerCharacter
{
	GENERATED_BODY()

public:
	AProject_JSpellbladeCharacter() = default;
};
