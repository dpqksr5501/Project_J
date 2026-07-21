#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Project_JGameplayEffect_CombatMode.generated.h"

/**
 * Persistent state effect applied by the shared combat-toggle ability.
 *
 * Keep all combat-presentational systems keyed from State.CombatMode rather
 * than from an ability lifetime: the toggle ability ends immediately, whereas
 * this infinite Gameplay Effect remains until the player exits combat. Its
 * Blueprint asset owns the UE 5.8 Target Tags component that grants the tag.
 */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API UProject_JGameplayEffect_CombatMode : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UProject_JGameplayEffect_CombatMode();
};
