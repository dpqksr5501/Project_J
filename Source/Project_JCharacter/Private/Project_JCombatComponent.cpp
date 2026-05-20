// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JCombatComponent.h"

UProject_JCombatComponent::UProject_JCombatComponent()
{
	// Disable ticking by default as it's not needed for base combat capabilities
	PrimaryComponentTick.bCanEverTick = false;
}
