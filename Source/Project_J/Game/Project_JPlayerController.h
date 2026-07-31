// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Project_JPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AProject_JPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	UFUNCTION(Exec)
	void DumpMMOState();

	UFUNCTION(Exec)
	void DumpAnimBudget();

	/** Prints the C++ locomotion kinematic analysis after movement has stopped. */
	UFUNCTION(Exec)
	void DumpLocomotionKinematics();

	/** Prints recent Motion Matching selection events captured while the player was moving. */
	UFUNCTION(Exec)
	void DumpMotionMatchingTrace();

	/** Prints native Motion Matching/BlendStack frames captured while p.ProjectJ.MMPivotDebug was enabled. */
	UFUNCTION(Exec)
	void DumpMotionMatchingPivotTrace();

	/** Prints native Motion Matching one-shot BlendStack frames captured while p.ProjectJ.MMTransitionDebug was enabled. */
	UFUNCTION(Exec)
	void DumpMotionMatchingTransitionTrace();

	UFUNCTION(Exec)
	void DumpReplicationPolicy();

	UFUNCTION(Exec)
	void DumpCharacterComponents();

	UFUNCTION(Exec)
	void DumpCombatState();

	UFUNCTION(Exec)
	void DumpMMOProfilingSnapshot(int32 MaxDetailedCharacters = 8);

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

};
