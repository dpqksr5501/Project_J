// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JAbilitySystemOwnerInterface.generated.h"

class UProject_JAbilitySystemComponent;
class UProject_JAttributeSet;

UINTERFACE(MinimalAPI, BlueprintType)
class UProject_JAbilitySystemOwnerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for actors that own a Project_J Ability System Component and Attribute Set.
 * Usually implemented by PlayerState.
 */
class PROJECT_JGAS_API IProject_JAbilitySystemOwnerInterface
{
	GENERATED_BODY()

public:
	/** Returns the specific Project_J Ability System Component */
	virtual UProject_JAbilitySystemComponent* GetProjectJAbilitySystemComponent() const = 0;

	/** Returns the specific Project_J Attribute Set */
	virtual UProject_JAttributeSet* GetProjectJAttributeSet() const = 0;

	virtual bool HasGrantedDefaultAbilities() const { return false; }
	virtual void SetHasGrantedDefaultAbilities(bool bGranted) {}
};
