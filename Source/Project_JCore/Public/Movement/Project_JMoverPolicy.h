#pragma once
#include "UObject/Interface.h"
#include "Project_JMoverPolicy.generated.h"
struct FProject_JMoverInputs;
struct FCharacterDefaultInputs;
struct FProject_JMoverPolicy
{
 float MaxSpeed=0.f;
 bool bStrafe=false;
 bool bSprint=false;
 bool bAllowJump=false;
 // Player commands cannot select a solver branch; native navigation can use Velocity.
 bool bRequireDirectionalIntent=false;
};
UINTERFACE(MinimalAPI)
class UProject_JMoverPolicySource : public UInterface { GENERATED_BODY() };
/** Local prediction and authority resolve the same gameplay permission at the physical boundary. */
class PROJECT_JCORE_API IProject_JMoverPolicySource
{
 GENERATED_BODY()
public:
 /** Authored limit for an already accepted gait; must not query physical state. */
 virtual float GetMoverMaxSpeed(bool bAcceptedSprint) const=0;
 /** Gait admission uses the recorded command, not a live controller/input cache.
  *  Input is optional for permission-only queries such as Jump(). */
 virtual FProject_JMoverPolicy ResolveMoverPolicy(const FProject_JMoverInputs& Requested,const FCharacterDefaultInputs* Input=nullptr) const=0;
};
