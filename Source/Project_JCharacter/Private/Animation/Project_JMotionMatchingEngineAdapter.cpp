#include "Animation/Project_JMotionMatchingEngineAdapter.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "UObject/UnrealType.h"

namespace ProjectJ::Animation
{
namespace
{
struct FFields
{
	FBoolProperty* Reset = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bResetOnBecomingRelevant"));
	FBoolProperty* Inertial = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bUseInertialBlend"));
	FFloatProperty* Blend = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("BlendTime"));
	FFloatProperty* Throttle = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("SearchThrottleTime"));
	FFloatProperty* Notify = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("NotifyRecencyTimeOut"));
	FFloatProperty* Override = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("MaxBlendInTimeToOverrideAnimation"));
	FFloatProperty* Depth = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("PlayerDepthBlendInTimeMultiplier"));
	FStructProperty* PlayRate = FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("PlayRate"));
	FStructProperty* State = FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("MotionMatchingState"));
	FFields() { check(Reset && Inertial && Blend && Throttle && Notify && Override && Depth && PlayRate && State); }
};
const FFields& Fields()
{
	// Native engine struct metadata is immutable during graph traversal. UE's
	// generated AnimBP node indices are still invalidated by the proxy on reinstance.
	static const FFields Value;
	return Value;
}
}

float GetSearchThrottleTime(const FAnimNode_MotionMatching& Node)
{ return Fields().Throttle->GetPropertyValue_InContainer(&Node); }
void SetSearchThrottleTime(FAnimNode_MotionMatching& Node, float Seconds)
{ Fields().Throttle->SetPropertyValue_InContainer(&Node, Seconds); }
FMotionMatchingState* GetMutableMotionMatchingState(FAnimNode_MotionMatching& Node)
{ return Fields().State->ContainerPtrToValuePtr<FMotionMatchingState>(&Node); }

void ApplyMotionMatchingPresentationPolicy(FAnimNode_MotionMatching& Node,
	const FProject_JMotionMatchingSearchPolicy& Policy, bool bInAir, bool bWasInAir, float VerticalSpeed)
{
	const auto& F = Fields();
	F.Blend->SetPropertyValue_InContainer(&Node, Policy.ResolveBlendTime(bInAir, bWasInAir, VerticalSpeed));
	F.Notify->SetPropertyValue_InContainer(&Node, FMath::Max(0.f, Policy.NotifyRecencyTimeOut));
	F.Override->SetPropertyValue_InContainer(&Node, FMath::Max(0.f, Policy.MaxBlendInTimeToOverrideAnimation));
	F.Depth->SetPropertyValue_InContainer(&Node, FMath::Max(.1f, Policy.PlayerDepthBlendInTimeMultiplier));
	Node.SetMaxActiveBlends(FMath::Clamp(Policy.MaxActiveBlends, 1, 8));
	auto& Rate = *F.PlayRate->ContainerPtrToValuePtr<FFloatInterval>(&Node);
	Rate.Min = FMath::Clamp(Policy.MinPlayRate, .2f, 3.f);
	Rate.Max = FMath::Clamp(FMath::Max(Rate.Min, Policy.MaxPlayRate), .2f, 3.f);
}

FScopedMotionMatchingReturn::FScopedMotionMatchingReturn(FAnimNode_MotionMatching& InNode,
	float Seconds, bool bInertial, bool bForceSearch) : Node(InNode)
{
	const auto& F = Fields();
	bReset = F.Reset->GetPropertyValue_InContainer(&Node);
	bOldInertial = F.Inertial->GetPropertyValue_InContainer(&Node);
	Blend = F.Blend->GetPropertyValue_InContainer(&Node);
	Throttle = GetSearchThrottleTime(Node);
	F.Reset->SetPropertyValue_InContainer(&Node, false);
	F.Inertial->SetPropertyValue_InContainer(&Node, bInertial);
	F.Blend->SetPropertyValue_InContainer(&Node, Seconds);
	if (bForceSearch) SetSearchThrottleTime(Node, 0.f);
}
FScopedMotionMatchingReturn::~FScopedMotionMatchingReturn()
{
	const auto& F = Fields();
	F.Reset->SetPropertyValue_InContainer(&Node, bReset);
	F.Inertial->SetPropertyValue_InContainer(&Node, bOldInertial);
	F.Blend->SetPropertyValue_InContainer(&Node, Blend);
	SetSearchThrottleTime(Node, Throttle);
}
}
