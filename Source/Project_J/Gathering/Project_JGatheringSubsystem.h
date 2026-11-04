#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JGatheringSubsystem.generated.h"

class AProject_JGatheringNode;
class AProject_JPlayerState;
struct FProject_JProfileDurableResult;

UENUM(BlueprintType)
enum class EProject_JGatheringPhase : uint8 { Available, Pending, Cooldown, Unresolved, Disabled };
struct FProject_JGatheringView
{
    EProject_JGatheringPhase Phase = EProject_JGatheringPhase::Disabled;
    double NextAvailable = 0;
};
struct FProject_JGatheringStats
{
    uint64 Accepted = 0, AdmissionRejected = 0, DuplicateOwners = 0, CapacityRejected = 0;
    uint64 Committed = 0, ProvenAbsent = 0, Unresolved = 0, StaleOutcomes = 0, Reclaimed = 0;
    int32 LastReclaimVisits = 0;
};

/** GT-only, bounded, no-Tick ownership for exclusive local gathering slots across Actor streaming lifetimes.
 * Owns no profile state/worker. Pending and unresolved slots never expire by time.
 * World teardown resets cooldowns; this is not a durable/distributed resource transaction. */
UCLASS()
class PROJECT_J_API UProject_JGatheringSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    static constexpr int32 MaxTrackedNodes = 2048;
    static constexpr int32 MaxReclaimsPerAdmission = 128;
    uint64 RegisterNode(AProject_JGatheringNode* Node);
    void UnregisterNode(FName Id, AProject_JGatheringNode* Node, uint64 Registration);
    FProject_JGatheringView GetState(FName Id, const AProject_JGatheringNode* Node) const;
    int32 GetTrackedCount() const { return Nodes.Num(); }
    const FProject_JGatheringStats& GetStats() const { return Stats; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldEndPlay(UWorld& World) override;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class AProject_JGatheringNode;
    friend class FProjectJGatheringExecutionTest;
    struct FSlot
    {
        TWeakObjectPtr<AProject_JGatheringNode> Owner;
        uint64 Registration = 0, Operation = 0;
        FGuid Request;
        EProject_JGatheringPhase Phase = EProject_JGatheringPhase::Available;
        double NextAvailable = 0, CapturedCooldown = 0;
        bool bTerminal = true;
        int32 ReclaimIndex = INDEX_NONE;
    };
    bool IsActive() const;
    void ReclaimExpired();
    void RemoveSlot(FName Id);
    void RemoveSlotAt(int32 Index);
    void Publish(FName Id);
    uint64 Reserve(AProject_JGatheringNode* Node, uint64 Registration, FGuid Request);
    void AbortUnsubmitted(FName Id, uint64 Operation, FGuid Request);
    void Complete(FName Id, uint64 Operation, FGuid Request, const FProject_JProfileDurableResult& Result);
    bool SubmitGather(AProject_JGatheringNode* Node, uint64 Registration, AProject_JPlayerState* Player);
    TMap<FName, FSlot> Nodes;
    TArray<FName> ReclaimOrder;
    int32 ReclaimCursor = 0;
    uint64 NextRegistration = 0, NextOperation = 0;
    bool bAccepting = false;
    FProject_JGatheringStats Stats;
};
