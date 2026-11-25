#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "Project_JMassMovementProcessor.generated.h"

/** No Actor, World, ASC or navigation pointers may enter this worker-owned fragment. */
USTRUCT()
struct PROJECT_JCHARACTER_API FProjectJMassMovementFragment : public FMassFragment
{
	GENERATED_BODY()
	FVector Position = FVector::ZeroVector;
	static constexpr int32 MaxRoutePoints = 32;
	FVector Route[MaxRoutePoints];
	int32 RouteCount = 0;
	uint64 StableId = 0, Generation = 1;
	int32 NextPoint = 0;
	float Speed = 300;
	float Radius = 42;
	float HalfHeight = 88;
	float BlockedSeconds = 0;
	uint16 NeighborVisits = 0;
	bool bNeedsCharacterTraversal = false;
	bool bMassOwnsMovement = false;
	bool bRouteValid = false;
};

/** Explicitly executed by the opt-in bridge, never applied to unrelated MassSpawner entities. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JMassMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UProject_JMassMovementProcessor();
	bool bParallel = true;
	/** Frame-local spatial snapshot; conservative longitudinal spacing never leaves the Nav corridor. */
	bool bUseCrowdSpacing = false;
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	struct FCrowdSample
	{
		FVector Position = FVector::ZeroVector;
		uint64 Id = 0;
		float Radius = 42.0f;
		float HalfHeight = 88.0f;
	};
	struct FCrowdCell
	{
		TArray<int32, TInlineAllocator<8>> Indices;
		bool bOverflow = false;
	};
	FMassEntityQuery Query;
	// Persistent scratch buffers reused across frames (zero steady-state heap allocations per tick).
	TArray<FCrowdSample> ReusableSamples;
	TArray<FCrowdCell> ReusableCells;
	TMap<FIntPoint, int32> ReusableGrid;
};
