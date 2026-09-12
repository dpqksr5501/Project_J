// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Project_JMessageSubsystem.generated.h"

// A simple delegate to pass generic message payloads. UObject* allows Blueprints to pass payload classes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JMessageDelegate, FGameplayTag, Channel, UObject*, Payload);

/**
 * Custom Message Router Subsystem for Event-Driven Architecture.
 * Game-thread, GameInstance-local notifications only; not a durable cross-server bus.
 * Listener changes during broadcast take effect on the next broadcast.
 */
UCLASS()
class PROJECT_JCORE_API UProject_JMessageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	/** Broadcast a message payload to all listeners on a specific channel */
	UFUNCTION(BlueprintCallable, Category = "Project_J|Messaging")
	void BroadcastMessage(FGameplayTag Channel, UObject* Payload);

	/** 
	 * Returns the delegate for a specific channel so other classes can bind to it. 
 * In C++, bind immediately with GetChannelDelegate(Tag).AddDynamic(...).
 * Do not retain the returned map reference across channel registration or dispatch.
	 */
	FProject_JMessageDelegate& GetChannelDelegate(FGameplayTag Channel);

	/** Clear all listeners for a channel */
	UFUNCTION(BlueprintCallable, Category = "Project_J|Messaging")
	void ClearChannel(FGameplayTag Channel);

private:
	// Map of channels (GameplayTags) to their respective multicast delegates
	UPROPERTY()
	TMap<FGameplayTag, FProject_JMessageDelegate> ListenerMap;
};
