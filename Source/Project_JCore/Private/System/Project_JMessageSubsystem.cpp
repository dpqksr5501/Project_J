// Fill out your copyright notice in the Description page of Project Settings.

#include "System/Project_JMessageSubsystem.h"

#include "Project_JCore.h"

void UProject_JMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogProject_JCore, Log, TEXT("Project_J Message Subsystem (Event Router) Initialized."));
}

void UProject_JMessageSubsystem::Deinitialize()
{
	ListenerMap.Empty();
	UE_LOG(LogProject_JCore, Log, TEXT("Project_J Message Subsystem Deinitialized."));
	Super::Deinitialize();
}

void UProject_JMessageSubsystem::BroadcastMessage(FGameplayTag Channel, UObject* Payload)
{
	check(IsInGameThread());
	if (!Channel.IsValid())
	{
		UE_LOG(LogProject_JCore, Warning, TEXT("Attempted to broadcast on an invalid GameplayTag channel."));
		return;
	}

	if (FProject_JMessageDelegate* DelegatePtr = ListenerMap.Find(Channel))
	{
		// Listeners may register another channel (rehashing ListenerMap), clear a
		// channel, or tear down this subsystem. Snapshot this dispatch's listeners.
		const FProject_JMessageDelegate Snapshot = *DelegatePtr;
		Snapshot.Broadcast(Channel, Payload);
	}
}

FProject_JMessageDelegate& UProject_JMessageSubsystem::GetChannelDelegate(FGameplayTag Channel)
{
	check(IsInGameThread());
	// FindOrAdd will return a reference to the existing delegate, or create a new one if it doesn't exist
	return ListenerMap.FindOrAdd(Channel);
}

void UProject_JMessageSubsystem::ClearChannel(FGameplayTag Channel)
{
	check(IsInGameThread());
	if (Channel.IsValid())
	{
		if (FProject_JMessageDelegate* DelegatePtr = ListenerMap.Find(Channel))
		{
			DelegatePtr->Clear();
		}
	}
}
