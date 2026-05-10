// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.generated.h"

/**
 * Custom Ability System Component for Project J.
 * Can be extended with custom logic for the MMORPG structure.
 */
UCLASS()
class PROJECT_J_API UProject_JAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	UProject_JAbilitySystemComponent();

	// Add any Project_J specific ASC logic here.
};
