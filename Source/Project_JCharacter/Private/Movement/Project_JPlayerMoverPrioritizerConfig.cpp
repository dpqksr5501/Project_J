#include "Movement/Project_JPlayerMoverPrioritizerConfig.h"

UProject_JPlayerMoverPrioritizerConfig::UProject_JPlayerMoverPrioritizerConfig()
{
	// At a 30Hz server, priority 0.5 reaches scheduling eligibility every two
	// ticks. OwnerPriorityBoost protects the owning client under 200-player congestion.
	OuterPriority = 0.5f;
	OutsidePriority = 0.5f;
	OwnerPriorityBoost = 2.0f;
}

UProject_JCombatActivePrioritizerConfig::UProject_JCombatActivePrioritizerConfig()
{
	InnerRadius = 2500.0f;
	OuterRadius = 10000.0f;
	InnerPriority = 3.0f;
	OuterPriority = 1.5f;
	OutsidePriority = 1.0f;
	OwnerPriorityBoost = 3.0f;
}

UProject_JCombatModePrioritizerConfig::UProject_JCombatModePrioritizerConfig()
{
	InnerRadius = 2500.0f;
	OuterRadius = 10000.0f;
	InnerPriority = 1.5f;
	OuterPriority = 0.75f;
	OutsidePriority = 0.5f;
	OwnerPriorityBoost = 2.0f;
}

UProject_JCombatDeadPrioritizerConfig::UProject_JCombatDeadPrioritizerConfig()
{
	InnerRadius = 2000.0f;
	OuterRadius = 8000.0f;
	InnerPriority = 0.25f;
	OuterPriority = 0.10f;
	OutsidePriority = 0.05f;
	OwnerPriorityBoost = 1.0f;
}

UProject_JCombatBossPrioritizerConfig::UProject_JCombatBossPrioritizerConfig()
{
	InnerRadius = 4000.0f;
	OuterRadius = 15000.0f;
	InnerPriority = 3.0f;
	OuterPriority = 2.0f;
	OutsidePriority = 1.0f;
	OwnerPriorityBoost = 2.0f;
}
