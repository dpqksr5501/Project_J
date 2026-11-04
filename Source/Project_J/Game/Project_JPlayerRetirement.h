#pragma once
#include "CoreMinimal.h"
class APlayerController;
namespace ProjectJ::Lifecycle
{
// Terminal connection cleanup only. Epoch rotation must not use this function.
// The caller supplies its exact-lease fence; callbacks may replace that lease.
bool RetireConnectionAvatar(APlayerController* Controller, TFunctionRef<bool()> IsRetirementCurrent);
}
