// Fill out your copyright notice in the Description page of Project Settings.

#include "System/Project_JAssetManager.h"

#include "Engine/Engine.h"
#include "Project_JCore.h"

UProject_JAssetManager::UProject_JAssetManager()
{
}

UProject_JAssetManager& UProject_JAssetManager::Get()
{
	check(GEngine);
	
	if (UProject_JAssetManager* Singleton = Cast<UProject_JAssetManager>(GEngine->AssetManager))
	{
		return *Singleton;
	}

	UE_LOG(LogProject_JCore, Fatal, TEXT("Invalid AssetManager in DefaultEngine.ini, must be Project_JAssetManager!"));
	return *NewObject<UProject_JAssetManager>(); // Should never reach here due to Fatal error
}

void UProject_JAssetManager::StartInitialLoading()
{
	Super::StartInitialLoading();

	// Output a log to confirm our custom Asset Manager has taken over.
	UE_LOG(LogProject_JCore, Log, TEXT("Project_J Asset Manager Initialized."));
	
	// Preload critical global data here if necessary.
}
