// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"
#include "Modules/ModuleManager.h"
#include "Backend/Project_JProfileDatabase.h"

class FProjectJGameModule final:public FDefaultGameModuleImpl
{
	void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if !UE_SERVER
		// OnlineSubsystemUtils asks for Voice on the first client connection even
		// when capture is disabled. Load this known dependency before Iris creates
		// a replication system, rather than changing serializer registrations live.
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("Voice"));
#endif
#if WITH_EDITOR || UE_SERVER
		// NetDriver lazily loads server counters on the first connection. Load
		// that known dependency before Iris freezes its serializer registrations.
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("PerfCounters"));
#endif
	}
	void ShutdownModule() override
	{
		// GameInstance persistence drains its native jobs before module unloading.
		FProject_JProfileDatabase::ShutdownVFS();
		FDefaultGameModuleImpl::ShutdownModule();
	}
};
IMPLEMENT_PRIMARY_GAME_MODULE( FProjectJGameModule, Project_J, "Project_J" );

DEFINE_LOG_CATEGORY(LogProject_J)
