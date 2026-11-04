#include "Movement/Project_JPlayerMoverPrioritizerConfig.h"

UProject_JPlayerMoverPrioritizerConfig::UProject_JPlayerMoverPrioritizerConfig()
{
	// At a 30Hz server, priority 0.5 reaches scheduling eligibility every two
	// ticks. This is a nominal cadence, not a delivery guarantee: packet budget,
	// root size and server time can still leave an individual endpoint starved.
	OuterPriority=.5f;
	OutsidePriority=.5f;
}
