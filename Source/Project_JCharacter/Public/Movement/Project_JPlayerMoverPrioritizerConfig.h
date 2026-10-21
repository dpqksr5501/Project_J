#pragma once

#include "Iris/ReplicationSystem/Prioritization/SphereNetObjectPrioritizer.h"
#include "Project_JPlayerMoverPrioritizerConfig.generated.h"

/** Player movement needs a receive cadence compatible with its interpolation
 * window even at the edge of relevancy. NPCs retain their own distance policy. */
UCLASS(Transient, Config=Engine)
class PROJECT_JCHARACTER_API UProject_JPlayerMoverPrioritizerConfig : public USphereNetObjectPrioritizerConfig
{
	GENERATED_BODY()
public:
	UProject_JPlayerMoverPrioritizerConfig();
};
