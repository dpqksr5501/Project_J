#pragma once

#include "Iris/ReplicationSystem/Prioritization/SphereWithOwnerBoostNetObjectPrioritizer.h"
#include "Project_JPlayerMoverPrioritizerConfig.generated.h"

/** Player movement needs a receive cadence compatible with its interpolation
 * window even at the edge of relevancy, plus owner boost under 200-player congestion. */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JPlayerMoverPrioritizerConfig : public USphereWithOwnerBoostNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JPlayerMoverPrioritizerConfig();
};

/** High-priority Iris tier for actors actively attacking, dodging, or hit-reacting (3.0x). */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JCombatActivePrioritizerConfig : public USphereWithOwnerBoostNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JCombatActivePrioritizerConfig();
};

/** Elevated Iris tier for actors in combat stance (1.5x). */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JCombatModePrioritizerConfig : public USphereWithOwnerBoostNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JCombatModePrioritizerConfig();
};

/** Low-priority Iris tier for dead actors (0.25x) to reclaim bandwidth in 200-player battles. */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JCombatDeadPrioritizerConfig : public USphereWithOwnerBoostNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JCombatDeadPrioritizerConfig();
};

/** Boss-tier Iris spatial prioritizer ensuring giant field/dungeon bosses never starve at arena edges. */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JCombatBossPrioritizerConfig : public USphereWithOwnerBoostNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JCombatBossPrioritizerConfig();
};
