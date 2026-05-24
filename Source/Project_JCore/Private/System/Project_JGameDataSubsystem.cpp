// Fill out your copyright notice in the Description page of Project Settings.

#include "System/Project_JGameDataSubsystem.h"

void UProject_JGameDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bIsReady = true;
	UE_LOG(LogTemp, Log, TEXT("Project_J GameData Subsystem Initialized."));
}

void UProject_JGameDataSubsystem::Deinitialize()
{
	bIsReady = false;
	UE_LOG(LogTemp, Log, TEXT("Project_J GameData Subsystem Deinitialized."));
	
	Super::Deinitialize();
}
