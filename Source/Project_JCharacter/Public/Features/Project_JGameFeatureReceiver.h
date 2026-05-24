#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JGameFeatureReceiver.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI, BlueprintType)
class UProject_JGameFeatureReceiver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for actors or components that need to respond when a GameFeature dynamically injects components into them.
 * Essential for the Pillar 6: Modular Gameplay and LiveOps.
 */
class PROJECT_JCHARACTER_API IProject_JGameFeatureReceiver
{
	GENERATED_BODY()

public:
	/**
	 * Called immediately after a GameFeature dynamically adds a component to the owner.
	 * @param AddedComponent The component that was just injected.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "GameFeatures")
	void OnGameFeatureComponentAdded(UActorComponent* AddedComponent);

	/**
	 * Called immediately before a GameFeature dynamically removes a component from the owner.
	 * @param RemovedComponent The component that is about to be removed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "GameFeatures")
	void OnGameFeatureComponentRemoved(UActorComponent* RemovedComponent);
};
