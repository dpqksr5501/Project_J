#include "Movement/Project_JPlayerMoverPrioritizerConfig.h"

UProject_JPlayerMoverPrioritizerConfig::UProject_JPlayerMoverPrioritizerConfig()
{
	// At a 30Hz server, priority 0.1 accumulated only one update per ten frames
	// (333ms). The 0.5 floor admits about 15Hz while nearby players retain 30Hz.
	// This preserves the 100ms interpolation target instead of adding latency.
	OuterPriority=.5f;
	OutsidePriority=.5f;
}
