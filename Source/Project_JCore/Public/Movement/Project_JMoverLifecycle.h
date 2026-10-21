#pragma once
#include "MoverDataModelTypes.h"
#include "Engine/NetSerialization.h"
#include "Project_JMoverLifecycle.generated.h"

/** Prediction-owned physical episodes; gameplay consumes each completed sequence once. */
USTRUCT()
struct PROJECT_JCORE_API FProject_JMoverLifecycle : public FMoverDataStructBase
{
 GENERATED_BODY()
 uint64 AirSequence=0;
 uint32 JumpSequence=0;
 uint64 LandingSequence=0;
 bool bJumpEpisode=false;
 bool bStrafe=false;
 bool bSprint=false;
 // Simulation-owned replacement for the sample BP's 0.2 second landing delay.
 float LandingBrakingTimeMs=0.f;
 void AdvanceLandingBraking(const FProject_JMoverLifecycle* Previous,float StepMs)
 {
  LandingBrakingTimeMs=LandingSequence && (!Previous || Previous->LandingSequence!=LandingSequence)?200.f:
   FMath::Max(0.f,(Previous?Previous->LandingBrakingTimeMs:0.f)-StepMs);
 }
 FVector_NetQuantize10 LandingVelocity=FVector::ZeroVector;
 virtual FMoverDataStructBase* Clone() const override { return new FProject_JMoverLifecycle(*this); }
 virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
 virtual bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& bSuccess) override
 {
  Ar<<AirSequence;Ar.SerializeIntPacked(JumpSequence);Ar<<LandingSequence;Ar<<LandingBrakingTimeMs;
  uint8 Flags=uint8(bJumpEpisode)|(uint8(bStrafe)<<1)|(uint8(bSprint)<<2);Ar.SerializeBits(&Flags,3);
  if(Ar.IsLoading()){bJumpEpisode=(Flags&1)!=0;bStrafe=(Flags&2)!=0;bSprint=(Flags&4)!=0;}
  return LandingVelocity.NetSerialize(Ar,Map,bSuccess);
 }
 virtual bool ShouldReconcile(const FMoverDataStructBase& Authority) const override
 {
  const auto& Other=static_cast<const FProject_JMoverLifecycle&>(Authority);
  return AirSequence!=Other.AirSequence || JumpSequence!=Other.JumpSequence || LandingSequence!=Other.LandingSequence || bJumpEpisode!=Other.bJumpEpisode || bStrafe!=Other.bStrafe || bSprint!=Other.bSprint || !FMath::IsNearlyEqual(LandingBrakingTimeMs,Other.LandingBrakingTimeMs,.1f);
 }
 virtual void Interpolate(const FMoverDataStructBase& From,const FMoverDataStructBase& To,float Pct) override
 { *this=static_cast<const FProject_JMoverLifecycle&>(Pct<.5f?From:To); }
};
template<> struct TStructOpsTypeTraits<FProject_JMoverLifecycle> : TStructOpsTypeTraitsBase2<FProject_JMoverLifecycle>
{ enum { WithCopy=true,WithNetSerializer=true }; };
