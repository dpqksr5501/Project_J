// Copyright Project J. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JProfilingCrowdComponent.generated.h"

class AProject_JPlayerCharacter;
class AAIController;

/**
 * Development-only crowd harness.
 *
 * It clones the possessed player-character class and drives deterministic
 * movement. The visual mode deliberately disables
 * replication; the replicated-movement mode runs only on the server and creates
 * simulated proxies on connected clients. Neither mode emulates player input
 * from many real client connections.
 */
UCLASS(ClassGroup = (Profiling), meta = (BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JProfilingCrowdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JProfilingCrowdComponent();

	bool Start(TSubclassOf<AProject_JPlayerCharacter> CharacterClass, const FVector& Center, const FVector& Forward, int32 RequestedCount);
	bool StartReplicatedMovement(TSubclassOf<AProject_JPlayerCharacter> CharacterClass, const FVector& Center, const FVector& Forward, int32 RequestedCount);
	void Stop();
	bool IsRunning() const { return SpawnedCharacters.Num() > 0; }
	bool IsReplicatedMovementProfile() const { return bReplicatedMovementProfile; }
	int32 GetSpawnedCount() const { return SpawnedCharacters.Num(); }
	int32 GetMovingCharacterCount() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool StartInternal(TSubclassOf<AProject_JPlayerCharacter> CharacterClass, const FVector& Center, const FVector& Forward, int32 RequestedCount, bool bReplicatedMovement);
	UPROPERTY(Transient)
	TArray<TObjectPtr<AProject_JPlayerCharacter>> SpawnedCharacters;

	// Non-ticking controllers keep local PIE movement on CMC's controlled-pawn path.
	UPROPERTY(Transient)
	TArray<TObjectPtr<AAIController>> SpawnedControllers;

	TArray<FVector> TravelAxes;
	TArray<float> PhaseOffsets;
	float ElapsedSeconds = 0.0f;
	bool bMovementHealthReported = false;
	bool bReplicatedMovementProfile = false;
};
