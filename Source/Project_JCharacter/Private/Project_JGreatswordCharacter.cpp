// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGreatswordCharacter.h"

#include "Project_JWarriorComponent.h"

AProject_JGreatswordCharacter::AProject_JGreatswordCharacter()
{
	GreatswordCombatComponent = CreateDefaultSubobject<UProject_JWarriorComponent>(TEXT("GreatswordCombatComponent"));
}
