#pragma once
#include "MoverTypes.h"
#include "Project_JMoverInputs.generated.h"

/** Requested presentation intent and jump identity travel with the predicted command. */
USTRUCT()
struct PROJECT_JCORE_API FProject_JMoverInputs : public FMoverDataStructBase
{
 GENERATED_BODY()
 bool bStrafe=false;
 bool bSprint=false;
 bool bWalk=false;
 // GASP enum order: Forward, Backward, LeftForward, LeftBackward, RightForward, RightBackward.
 uint8 MovementDirection=0;
 float RotationOffset=0.f;
 float ControlRotationRate=0.f;
 uint32 JumpRequestId=0;
 virtual FMoverDataStructBase* Clone() const override { return new FProject_JMoverInputs(*this); }
 virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
 virtual bool NetSerialize(FArchive& Ar,UPackageMap*,bool& bSuccess) override
 {
  uint8 Flags=uint8(bStrafe)|(uint8(bSprint)<<1)|(uint8(bWalk)<<2);Ar.SerializeBits(&Flags,3);
  if(Ar.IsLoading()){bStrafe=(Flags&1)!=0;bSprint=(Flags&2)!=0;bWalk=(Flags&4)!=0;}
  Ar.SerializeBits(&MovementDirection,3);Ar<<RotationOffset;Ar<<ControlRotationRate;
  Ar.SerializeIntPacked(JumpRequestId);
  bSuccess=!Ar.IsError() && MovementDirection<6 && FMath::IsFinite(RotationOffset) && FMath::IsFinite(ControlRotationRate);
  return bSuccess;
 }
 virtual bool ShouldReconcile(const FMoverDataStructBase& Other) const override
 {const auto& Value=static_cast<const FProject_JMoverInputs&>(Other);return bStrafe!=Value.bStrafe || bSprint!=Value.bSprint || bWalk!=Value.bWalk || MovementDirection!=Value.MovementDirection || RotationOffset!=Value.RotationOffset || ControlRotationRate!=Value.ControlRotationRate || JumpRequestId!=Value.JumpRequestId;}
 virtual void Interpolate(const FMoverDataStructBase& From,const FMoverDataStructBase& To,float Pct) override
 {*this=static_cast<const FProject_JMoverInputs&>(Pct<1.f?From:To);}
 virtual void Merge(const FMoverDataStructBase&) override {}
};
template<> struct TStructOpsTypeTraits<FProject_JMoverInputs> : TStructOpsTypeTraitsBase2<FProject_JMoverInputs>
{enum{WithNetSerializer=true,WithCopy=true};};
