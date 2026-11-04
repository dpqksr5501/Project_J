#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Project_JGameplayAbility.generated.h"

/** Project abilities participating in exact native client-payload lifetime admission. */
UCLASS(Abstract)
class PROJECT_JGAS_API UProject_JGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** Read native state without duplicating it; valid during pre-count activation and End callbacks. */
	bool CanReceiveClientExecutionData() const
	{
		return IsInstantiated() && bIsActive && !bIsAbilityEnding;
	}
};
