#pragma once
#include "CoreMinimal.h"
class UGameplayEffect;
namespace Project_J::Combat
{
/** GT-only confirmed contact dispatch. Geometry and execution ownership remain with the caller.
 * Returns spec submission, not positive damage (immunity/mitigation may reject it). */
PROJECT_JCHARACTER_API bool ApplyAuthoritativeDamage(AActor* Source, AActor* Target,
 TSubclassOf<UGameplayEffect> Effect, TFunctionRef<bool()> IsCurrent);
}
