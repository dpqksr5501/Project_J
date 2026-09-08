#include "Project_JCore.h"
#include "Project_JGameplayTags.h"
#include "Optimization/Project_JTargetScoring.h"

DEFINE_LOG_CATEGORY(LogProject_JCore);

#define LOCTEXT_NAMESPACE "FProject_JCoreModule"

void FProject_JCoreModule::StartupModule()
{
	FProject_JGameplayTags::InitializeNativeGameplayTags();
}

void FProject_JCoreModule::ShutdownModule()
{
	ProjectJ::TargetScoring::DrainTasksForModuleShutdown();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FProject_JCoreModule, Project_JCore)
