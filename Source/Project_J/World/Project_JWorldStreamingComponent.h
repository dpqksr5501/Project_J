#pragma once
#include "CoreMinimal.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Project_JWorldStreamingComponent.generated.h"

class UProject_JWorldArrivalSubsystem;
class AProject_JWorldState;
class UProject_JWorldDefinition;
class AProject_JWorldPortal;
class APawn;
class APlayerState;
class UProject_JAbilitySystemComponent;
class UMoverComponent;
struct FProject_JPreparedArrivalEffect;

UENUM(BlueprintType)
enum class EProject_JWorldArrivalPhase : uint8 { Idle, Preparing, Queued, AwaitingLocalArrival, Arrived, Failed, ExpiredUnconfirmed };

/** Additional local-player source. Does not disable the controller's default source or block normal movement. */
UCLASS(ClassGroup=(World), meta=(BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JWorldStreamingComponent : public UWorldPartitionStreamingSourceComponent
{
    GENERATED_BODY()
public:
    UProject_JWorldStreamingComponent();
    virtual bool GetStreamingSource(FWorldPartitionStreamingSource& Out) const override;
    /** Opaque WP provider identity; a component path remains diagnostic text only. */
    FName GetStreamingSourceIdentity() const;
    /** Trusted authority entry used by the normal Portal interaction; never accepts client coordinates. */
    bool RequestPreparedArrival(AProject_JWorldPortal* Portal, APawn* Interactor);
    /** Current request has permission to advertise its destination; not readiness or I/O completion. */
    bool IsPreparedArrivalStarted() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Arrival") EProject_JWorldArrivalPhase ArrivalPhase = EProject_JWorldArrivalPhase::Idle;
    static FVector PredictOffset(const FVector& Velocity, float Seconds, float Maximum);
    static bool BuildPreparationShapes(const FVector& Velocity, float Seconds, float Radius, float Maximum,
        TArray<FStreamingSourceShape>& OutShapes);
protected:
    virtual void OnRegister() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
private:
    friend class FProjectJWorldPreparationLifetimeTest;
    friend struct FProject_JPreparedArrivalEffect;
    friend class FProjectJWorldPreparedArrivalTest;
    friend class FProjectJWorldArrivalExecutionBudgetTest;
    friend class UProject_JWorldArrivalSubsystem;
    bool RegisterArrivalExecution();
    bool TryStartArrivalPreparation();
    void AdvancePreparedArrival(double Now);
    static constexpr double ArrivalTimeoutSeconds = 30;
    static constexpr int32 MaxArrivalCells = 128;
    bool IsArrivalCurrent(bool bRequireOrigin) const;
    bool IsArrivalRegionReady() const;
    bool TakeArrivalReadySend(double Now);
    bool FindArrivalPlacement(APawn* Interactor, const FVector& Hint, FVector& Placement) const;
    bool IsArrivalPlacementValid(APawn* Interactor, const FVector& Origin, const FQuat& Rotation) const;
    bool IsArrivalPlacementCurrent(APawn* Interactor, const FVector& Expected) const;
    void RecordArrivalOutcome(uint64 Request, APawn* ExpectedPawn, const UMoverComponent* ExpectedMover, bool bSucceeded);
    void FinishArrival(EProject_JWorldArrivalPhase Phase, bool bNotifyClient);
    UFUNCTION(Client, Reliable) void ClientPrepareArrival(uint64 Request, FVector Hint, float Radius);
    UFUNCTION(Server, Reliable) void ServerReadyForArrival(uint64 Request);
    UFUNCTION(Client, Reliable) void ClientFinishArrival(uint64 Request, bool bSucceeded, FVector Placement);
    // Non-reflected registration lifetime, unrelated to saved data or request IDs.
    FName StreamingSourceIdentity;
    uint64 ArrivalRequest = 0, NextArrivalRequest = 0;
    uint64 ArrivalExecutionToken = 0;
    TWeakObjectPtr<UProject_JWorldArrivalSubsystem> ArrivalExecutionOwner;
    TWeakObjectPtr<AProject_JWorldPortal> ArrivalPortal;
    TWeakObjectPtr<APawn> ArrivalPawn;
    TWeakObjectPtr<APlayerState> ArrivalPlayerState;
    // Authority-local recipient incarnation; never compared with a client ASC epoch.
    TWeakObjectPtr<UProject_JAbilitySystemComponent> ArrivalAbilitySystem;
    uint64 ArrivalActorInfoRevision = 0;
    TWeakObjectPtr<const UProject_JWorldDefinition> ArrivalDefinition;
    FName ArrivalPlaceId;
    FGuid ArrivalAccountId, ArrivalCharacterId;
    FVector ArrivalHint = FVector::ZeroVector, ArrivalPlacement = FVector::ZeroVector;
    float ArrivalRadius = 0;
    // Independent component instances reuse the same deadline for client retry / authority domain admission.
    double ArrivalDeadline = 0, NextClientArrivalAck = 0;
    // Source permission follows an exact authority Entry permit; a client receives it through the existing trusted RPC.
    bool bArrivalPreparationStarted = false;
    bool bClientArrivalReady = false, bArrivalOutcomeReceived = false, bArrivalOutcomeSucceeded = false;
    const UProject_JWorldDefinition* ResolveWorldDefinition() const;
    mutable TWeakObjectPtr<AProject_JWorldState> CachedWorldState;
    TWeakObjectPtr<const UProject_JWorldDefinition> InitializedDefinition;
    mutable double NextDefinitionSearch = 0;
public:
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="0",ClampMax="10")) float PredictionSeconds = 3;
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="1000",ClampMax="200000")) float PreparationRadius = 60000;
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="0",ClampMax="200000")) float MaxPredictionDistance = 60000;
};
