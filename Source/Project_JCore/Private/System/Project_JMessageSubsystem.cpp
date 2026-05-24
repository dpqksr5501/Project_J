// Fill out your copyright notice in the Description page of Project Settings.

#include "System/Project_JMessageSubsystem.h"

void UProject_JMessageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("Project_J Message Subsystem (Event Router) Initialized."));
}

void UProject_JMessageSubsystem::Deinitialize()
{
	ListenerMap.Empty();
	UE_LOG(LogTemp, Log, TEXT("Project_J Message Subsystem Deinitialized."));
	Super::Deinitialize();
}

void UProject_JMessageSubsystem::BroadcastMessage(FGameplayTag Channel, UObject* Payload)
{
	if (!Channel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Attempted to broadcast on an invalid GameplayTag channel."));
		return;
	}

	if (FProject_JMessageDelegate* DelegatePtr = ListenerMap.Find(Channel))
	{
		DelegatePtr->Broadcast(Channel, Payload);
	}
}

FProject_JMessageDelegate& UProject_JMessageSubsystem::GetChannelDelegate(FGameplayTag Channel)
{
	// FindOrAdd will return a reference to the existing delegate, or create a new one if it doesn't exist
	return ListenerMap.FindOrAdd(Channel);
}

void UProject_JMessageSubsystem::ClearChannel(FGameplayTag Channel)
{
	if (Channel.IsValid())
	{
		if (FProject_JMessageDelegate* DelegatePtr = ListenerMap.Find(Channel))
		{
			DelegatePtr->Clear();
		}
	}
}
