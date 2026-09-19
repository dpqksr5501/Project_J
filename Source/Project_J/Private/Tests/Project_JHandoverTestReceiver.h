#pragma once

#include "CoreMinimal.h"
#include "Backend/Project_JHandoverManager.h"
#include "Project_JHandoverTestReceiver.generated.h"

// Private automation fixture; never registered or ticked by gameplay.
UCLASS()
class UProject_JHandoverTestReceiver : public UObject
{
	GENERATED_BODY()
public:
	TFunction<void(FGuid, EProject_JHandoverState)> OnState;
	UFUNCTION()
	void HandleState(FGuid TransferId, EProject_JHandoverState State, EProject_JHandoverFailureReason FailureReason)
	{
		if (OnState) { OnState(TransferId, State); }
	}
};
