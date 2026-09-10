#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "NavigationData.h"
#include "Project_JMassRepresentationSubsystem.generated.h"

struct FMassEntityManager;
class UProject_JMassMovementProcessor;
class AProject_JNPCCharacter;
class UProject_JNPCActionComponent;
class UProject_JTargetScoringComponent;
struct FProjectJMassRepresentationEntry;

struct FProjectJMassAgentToken
{
	uint64 Id = 0, Generation = 0, WorldEpoch = 0;
	bool IsValid() const { return Id != 0 && Generation != 0 && WorldEpoch != 0; }
};
struct FProjectJMassRepresentationStats
{
	uint64 Promoted = 0, Demoted = 0, RejectedStale = 0, Deferred = 0;
	int32 Registered = 0, MassOwned = 0, LastTransitions = 0;
	double LastStepMilliseconds = 0;
};

/**
 * Opt-in movement handoff. Retains the Character's ASC/equipment identity while its movement,
 * collision and pose are suspended. This reduces work, not Actor memory/GC count.
 * Gameplay state stays on the Character; only validated route values enter Mass.
 * Private entity manager + joined chunk execution prevents overlap with global Mass phases.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JMassRepresentationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxAgents = 2048;
	static constexpr int32 MaxTransitionsPerTick = 4;
	static constexpr double PromoteDistance = 1500;
	static constexpr double DemoteDistance = 2200;
	/** Caller explicitly lends movement ownership. No automatic migration of existing NPCs. */
	FProjectJMassAgentToken RegisterNPC(AProject_JNPCCharacter* NPC);
	FProjectJMassAgentToken GetToken(uint64 Id) const;
	/** Supply an engine path produced on GT; invalidation freezes Mass movement and requests promotion. */
	bool SetRoute(FProjectJMassAgentToken Token, const FNavPathSharedPtr& Path);
	bool UnregisterNPC(FProjectJMassAgentToken Token);
	bool IsMassOwned(uint64 Id) const;
	const FProjectJMassRepresentationStats& GetStats() const { return Stats; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
#if WITH_DEV_AUTOMATION_TESTS
	void SetObserversForTest(TArray<FVector> InObservers) { TestObservers = MoveTemp(InObservers); bTestObservers = true; }
#endif
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	bool CanDemote(const FProjectJMassRepresentationEntry& Entry) const;
	void Demote(FProjectJMassRepresentationEntry& Entry);
	void Promote(FProjectJMassRepresentationEntry& Entry, bool bResumeActions);
	void Stop();
	void OnTearDown(UWorld* World);
	TSharedPtr<FMassEntityManager> Entities;
	FMassArchetypeHandle Archetype;
	UPROPERTY(Transient) TObjectPtr<UProject_JMassMovementProcessor> Processor;
	TArray<TSharedPtr<FProjectJMassRepresentationEntry>> Entries;
	FProjectJMassRepresentationStats Stats;
	uint64 NextId = 0, WorldEpoch = 0;
	int32 Cursor = 0;
	bool bAccepting = false, bStepping = false;
	FDelegateHandle TearDownHandle;
#if WITH_DEV_AUTOMATION_TESTS
	TArray<FVector> TestObservers;
	bool bTestObservers = false;
#endif
};
