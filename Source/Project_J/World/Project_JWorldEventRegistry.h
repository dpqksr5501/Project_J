#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "Project_JWorldEventRegistry.generated.h"

class AProject_JWorldEncounter;
class UActorComponent;
struct FStreamableHandle;

struct FProject_JWorldEventStats
{
    uint64 Updates = 0, SnapshotBuilds = 0, PreparationDeferrals = 0, RejectedRegistrations = 0;
    uint64 SpawnApplications = 0;
    uint64 ObserverOverflows = 0, ExpiredPreparations = 0;
    int32 LastTickVisits = 0, LastTickUpdates = 0, LastTickSpawns = 0, Observers = 0;
    int32 Preparations = 0, PeakPreparations = 0;
    double LastTickMilliseconds = 0, MaxUpdateLatenessSeconds = 0;
};

struct FProject_JWorldEventEntry
{
    TWeakObjectPtr<AActor> Owner;
    double NextUpdate = 0;
    // Derived slot only; Owners remains the authority for identity and lifetime.
    int32 VisitIndex = INDEX_NONE;
    // Non-owning execution order; dense slots may move without changing a sweep.
    FName PreviousVisit, NextVisit;
};
struct FProject_JWorldEventPreparation
{
    TWeakObjectPtr<UObject> Owner;
    TFunction<void(uint64, bool)> Completion;
    TSharedPtr<FStreamableHandle> Handle;
    double Deadline = 0;
    bool bReady = false;
};

/** Authority-local identity and bounded encounter execution. Encounter actors remain the state owners.
 * Not a distributed scheduler or persistent event service. Async reservations survive owner cancellation. */
UCLASS()
class PROJECT_J_API UProject_JWorldEventRegistry : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    bool Register(FName Id, AActor* Owner);
    void Unregister(FName Id, AActor* Owner);
    static constexpr int32 MaxEvents = 2048;
    static constexpr int32 MaxVisitsPerTick = 128;
    static constexpr int32 MaxUpdatesPerTick = 8;
    static constexpr int32 MaxPreparations = 8;
    static constexpr int32 MaxSpawnsPerTick = 2;
    static constexpr double UpdateBudgetMilliseconds = 1.0;
    static constexpr double UpdateIntervalSeconds = 1.0;
    static constexpr double PreparationTimeoutSeconds = 30.0;
    bool HasNearbyPlayer(const FVector& Origin, double Radius);
    /** True only for a complete query; bFound stays false on unknown/invalid coverage. */
    bool TryQueryNearbyPlayer(const FVector& Origin, double Radius, bool& bFound);
    uint64 RequestPreparation(AProject_JWorldEncounter* Owner, const FSoftObjectPath& ClassPath);
    /** Same physical load/apply budget for session-owned actor creation. No callback runs in the async completion. */
    uint64 RequestComponentPreparation(UActorComponent* Owner, const FSoftObjectPath& ClassPath, TFunction<void(uint64, bool)> Completion);
    void ReleasePreparation(uint64 Token, UObject* Owner);
    const FProject_JWorldEventStats& GetStats() const { return Stats; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldEndPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class AProject_JWorldEncounter;
	friend class FProjectJWorldEventExecutionTest;
    friend class FProjectJWorldEventFairChurnTest;
    void Stop();
    void InvalidateEventDeadline();
    void AddVisit(FName Id, FProject_JWorldEventEntry& Entry);
    void RemoveVisit(FProject_JWorldEventEntry& Entry);
    void OnTearDown(UWorld* World);
    bool IsCurrent(const AProject_JWorldEncounter* Owner) const;
    bool IsPreparationCurrent(UObject* Owner) const;
    uint64 BeginPreparation(UObject* Owner, const FSoftObjectPath& ClassPath, TFunction<void(uint64, bool)> Completion,
        const FSoftObjectPath& AdditionalPath = FSoftObjectPath());
    TMap<FName, FProject_JWorldEventEntry> Owners;
    TArray<FName> VisitOrder;
    TMap<uint64, FProject_JWorldEventPreparation> Preparations;
    ProjectJ::TargetScoring::FTargetSpatialSnapshot Players;
    FProject_JWorldEventStats Stats;
    FDelegateHandle TearDownHandle;
    uint64 NextPreparationToken = 0;
    int32 Cursor = 0;
    double NextObserverSnapshot = 0;
    // Reuse the bounded round-robin sweep to find its next due time. Never
    // sleep while unvisited owners or a changed membership can hide due work.
    uint64 ScheduleRevision = 0;
    int32 DeadlineVisitsRemaining = 0;
    double CandidateEventWake = 0, NextEventWake = 0, NextPreparationWake = 0;
    double LastEventClock = 0;
    bool bAccepting = false, bTicking = false, bCompleteObservers = false;
};
