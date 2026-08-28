// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JMountedAnimationLayerComponent.generated.h"

class AProject_JMountCharacter;
struct FStreamableHandle;

/**
 * Rendering-client-only owner of the rider's MountedLocomotion linked layer.
 *
 * The mounted actor selects a shared rider profile. This component streams and
 * links that profile's layer without adding mount-specific branches to the
 * player master AnimBP or doing synchronous loads at mount time.
 */
UCLASS(ClassGroup = (Mount), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JMountedAnimationLayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JMountedAnimationLayerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Re-evaluates the currently mounted actor and links its ready layer. */
	UFUNCTION(BlueprintCallable, Category = "Mount|Animation")
	void RefreshLayer();

	/**
	 * Starts streaming a candidate mount layer ahead of the actual mount event.
	 * Interaction/summon presentation may call this to eliminate first-use delay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mount|Animation")
	void PreloadLayerForMount(const AProject_JMountCharacter* Mount);

	UFUNCTION(BlueprintPure, Category = "Mount|Animation")
	bool IsMountedLayerLinked() const { return LinkedAnimationLayerClass != nullptr; }

private:
	UFUNCTION()
	void HandleMountChanged(AProject_JMountCharacter* PreviousMount, AProject_JMountCharacter* NewMount);

	FSoftObjectPath ResolveLayerPath(const AProject_JMountCharacter* Mount) const;
	void HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath);
	void UnlinkLayer();
	void ResetPreload();

	/** Hard reference held only while the matching linked instance is active. */
	TSubclassOf<class UAnimInstance> LinkedAnimationLayerClass;

	/** Single-entry cache: a player can have only one active/summoned mount. */
	UPROPERTY(Transient)
	TSubclassOf<class UAnimInstance> PreloadedAnimationLayerClass;

	FSoftObjectPath PreloadedAnimationLayerPath;
	TSharedPtr<FStreamableHandle> LayerPreloadHandle;
};
