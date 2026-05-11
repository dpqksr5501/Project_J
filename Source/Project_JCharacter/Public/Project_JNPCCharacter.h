// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JBaseCharacter.h"
#include "Project_JNPCCharacter.generated.h"

/**
 * Base character class for NPCs and Monsters in Project J.
 * Does NOT inherit from MotionMatchingCharacter, as they do not use motion matching.
 */
UCLASS()
class PROJECT_JCHARACTER_API AProject_JNPCCharacter : public AProject_JBaseCharacter
{
	GENERATED_BODY()

public:
	AProject_JNPCCharacter();

protected:
	virtual void BeginPlay() override;

	// Add AI specific properties or components here
};
