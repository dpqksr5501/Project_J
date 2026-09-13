#pragma once
#include "CoreMinimal.h"
class AActor;
class ACharacter;

namespace Project_J::Interaction
{
	// Server-owned instant interaction. Hold/exclusive sessions require their own action lifecycle.
	bool TryInteract(ACharacter& Interactor, float SearchRadius = 300.0f);
	bool IsEligible(ACharacter& Interactor, AActor* Candidate, float SearchRadius);
}
