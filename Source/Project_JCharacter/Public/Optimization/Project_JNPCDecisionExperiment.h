#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JNPCDecisionExperiment.generated.h"

/** Explicit native sandbox. Spawns only when requested and destroys only its own actors. */
UCLASS(NotBlueprintable)
class PROJECT_JCHARACTER_API AProject_JNPCDecisionExperiment : public AActor
{
	GENERATED_BODY()
public:
	AProject_JNPCDecisionExperiment();
	bool StartExperiment(int32 NPCCount = 64, int32 TargetCount = 32);
	void StopExperiment();
	FString GetSummary() const;
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	TArray<TWeakObjectPtr<AActor>> SpawnedActors;
	TArray<TWeakObjectPtr<AActor>> RegisteredTargets;
};
