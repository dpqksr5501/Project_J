#pragma once
#include "CoreMinimal.h"
class AActor;
class APawn;

namespace Project_J::Interaction
{
	// Server-owned instant interaction. Hold/exclusive sessions require their own action lifecycle.
	bool TryInteract(APawn& Interactor, float SearchRadius = 300.0f);
	bool IsEligible(APawn& Interactor, AActor* Candidate, float SearchRadius);
}
