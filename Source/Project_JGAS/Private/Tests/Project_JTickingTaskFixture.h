#pragma once

#include "GameplayTask.h"
#include "Project_JTickingTaskFixture.generated.h"

/** Asset-independent test task, never created by gameplay. */
UCLASS(Transient, NotBlueprintable)
class UProject_JTickingTaskFixture : public UGameplayTask
{
	GENERATED_BODY()
public:
	UProject_JTickingTaskFixture(const FObjectInitializer& Initializer = FObjectInitializer::Get()) : Super(Initializer)
	{
		bTickingTask = true;
	}
	virtual void TickTask(float DeltaTime) override { ++TickCount; }
	void InitializeForTest(IGameplayTaskOwnerInterface& Owner) { InitTask(Owner, 127); }
	int32 TickCount = 0;
};
