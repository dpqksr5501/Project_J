#include "Mass/Project_JMassMonsterSpawner.h"

AProject_JMassMonsterSpawner::AProject_JMassMonsterSpawner()
{
	// You can set default spawn counts or attach custom C++ components here.
	// For example, default to spawning 100 entities:
	Count = 100;
	
	// Example: By default, you can force the spawner to auto-spawn on BeginPlay
	bAutoSpawnOnBeginPlay = true;
}

void AProject_JMassMonsterSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Additional C++ logic before or after spawning can go here.
}
