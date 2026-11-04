#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JWorldArrivalSubsystem.generated.h"

class UProject_JWorldStreamingComponent;

/** GT poll/commit bookkeeping only. Not WP I/O, source or engine-task reservations. */
struct FProjectJWorldArrivalExecutionStats
{
    int32 Pending = 0, PeakPending = 0;
    // Authority logical permits only. Client completion and native I/O may outlive them.
    int32 ActivePreparationPermits = 0, PeakPreparationPermits = 0;
    uint64 PreparationAdmissions = 0, PreparationDeferrals = 0;
    int32 FrameVisits = 0, FrameAdvances = 0, PeakFrameVisits = 0, PeakFrameAdvances = 0;
    uint64 Frame = MAX_uint64, Registered = 0, Rejected = 0, Retired = 0, BudgetStops = 0;
    uint64 Advances = 0, LateAdvances = 0, DeadlineObservedLate = 0;
    double FrameMilliseconds = 0, PeakFrameMilliseconds = 0, MaxDueDelayMilliseconds = 0;
};

/** One production writer for prepared-arrival polling/commit, in each Game/PIE world. */
UCLASS()
class PROJECT_J_API UProject_JWorldArrivalSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static constexpr int32 MaxPending = 256;
    static constexpr int32 MaxFrameVisits = 64;
    static constexpr int32 MaxFrameAdvances = 4;
    static constexpr double PollIntervalSeconds = .1;
    static constexpr double SoftFrameMilliseconds = .5;
    const FProjectJWorldArrivalExecutionStats& GetStats() const { return Stats; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldEndPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class UProject_JWorldStreamingComponent;
    friend class FProjectJWorldArrivalExecutionBudgetTest;
    friend class FProjectJWorldPreparationLifetimeTest;
    struct FEntry
    {
        TWeakObjectPtr<UProject_JWorldStreamingComponent> Component;
        uint64 Request = 0, Previous = 0, Next = 0, VisitedFrame = MAX_uint64;
        double NextDue = 0;
        bool bPreparationAdmitted = false;
    };
    FName AllocateStreamingSourceIdentity();
    uint64 Register(UProject_JWorldStreamingComponent* Component, uint64 Request);
    void Unregister(uint64 Token, UProject_JWorldStreamingComponent* Component, uint64 Request);
    bool TryAdmitPreparation(uint64 Token, UProject_JWorldStreamingComponent* Component, uint64 Request, int32 Limit);
    bool HasPreparationPermit(uint64 Token, const UProject_JWorldStreamingComponent* Component, uint64 Request) const;
    void Remove(uint64 Token);
    void Stop();
    void OnTearDown(UWorld* World);
    TMap<uint64, FEntry> Entries;
    uint64 Head = 0, Cursor = 0, LastToken = 0, AdmissionFrontier = 0;
    // Independent provider identity: request tokens change on every arrival, while
    // WP needs one stable identity throughout a component registration.
    FName StreamingSourceNamespace;
    int32 LastStreamingSourceNumber = 0;
    bool bAccepting = false, bTicking = false;
    FDelegateHandle TearDownHandle;
    FProjectJWorldArrivalExecutionStats Stats;
};
