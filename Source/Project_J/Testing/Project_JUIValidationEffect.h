#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Project_JUIValidationEffect.generated.h"

/** Opt-in fixture effect with a replicable native definition. Never granted by normal gameplay. */
UCLASS()
class UProject_JUIValidationEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UProject_JUIValidationEffect();
};
