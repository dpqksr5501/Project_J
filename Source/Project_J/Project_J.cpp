// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"
#include "Modules/ModuleManager.h"
#include "Backend/Project_JProfileDatabase.h"

class FProjectJGameModule final:public FDefaultGameModuleImpl
{
	void ShutdownModule() override
	{
		// GameInstance persistence drains its native jobs before module unloading.
		FProject_JProfileDatabase::ShutdownVFS();
		FDefaultGameModuleImpl::ShutdownModule();
	}
};
IMPLEMENT_PRIMARY_GAME_MODULE( FProjectJGameModule, Project_J, "Project_J" );

DEFINE_LOG_CATEGORY(LogProject_J)
