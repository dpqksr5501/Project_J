#pragma once
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "Project_JPreparedArrivalEffect.generated.h"
class UProject_JWorldStreamingComponent;
class APawn;

/** One prepared-arrival request on the configured synchronous Mover backend. */
USTRUCT()
struct FProject_JPreparedArrivalEffect : public FTeleportEffect
{
    GENERATED_BODY()
    UPROPERTY() uint64 Request = 0;
    // Server-local receipt context is deliberately not part of network input.
    TWeakObjectPtr<UProject_JWorldStreamingComponent> Recipient;
    TWeakObjectPtr<APawn> ExpectedPawn;
    virtual bool ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& Output) override;
    virtual bool ApplyMovementEffect_Async(FApplyMovementEffectParams_Async&, FMoverSyncState&) override { return false; }
    virtual FInstantMovementEffect* Clone() const override { return new FProject_JPreparedArrivalEffect(*this); }
    virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
    virtual void NetSerialize(FArchive& Ar) override { Super::NetSerialize(Ar); Ar << Request; }
};
