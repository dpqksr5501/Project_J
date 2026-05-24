// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Project_JAssetManager.generated.h"

/**
 * Custom Asset Manager for Project_J.
 * Responsible for asynchronous loading of PrimaryDataAssets, Soft Pointers,
 * and managing memory footprint for large game data like items, skills, and animations.
 */
UCLASS()
class PROJECT_JCORE_API UProject_JAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	UProject_JAssetManager();

	// Returns the AssetManager singleton object
	static UProject_JAssetManager& Get();

protected:
	// Called when the asset manager is created to setup its systems
	virtual void StartInitialLoading() override;
};
