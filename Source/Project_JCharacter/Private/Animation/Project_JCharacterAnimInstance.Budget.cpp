#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Project_JBaseCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
FProject_JAnimOptimizationPolicy UProject_JCharacterAnimInstance::BuildOptimizationPolicy() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimBuildOptimizationPolicy);
	ProjectJ::Animation::FQualityObservation Input;
	Input.bHasOwner = OwningCharacter != nullptr;
	FProject_JAnimationBudgetSettings Settings;
	if (OwningCharacter)
	{
		Input.bLocal = IsLocallyControlledCharacter();
		if (!Input.bLocal)
		{
			if (const auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(OwningCharacter->GetMesh()))
				Input.bGameplayPose = Mesh->IsCombatCritical() || Mesh->HasAnimationUpdateRequirement();
			if (!Input.bGameplayPose)
			{
				Input.bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
				if (Input.bRecentlyRendered)
					if (const auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(OwningCharacter->GetMesh()))
					{ Input.bHasSignificance = true; Input.Significance = Mesh->GetAnimationDistanceTier(); }
					else if (const auto* Base = Cast<AProject_JBaseCharacter>(OwningCharacter))
					{ Input.bHasSignificance = true; Input.Significance = Base->GetSignificance(); }
				if (!Input.bRecentlyRendered || (Input.bHasSignificance && !(Input.Significance <= 0.f)))
					Settings = GetEffectiveAnimationBudgetSettings();
			}
		}
	}
	return ProjectJ::Animation::ResolveSourceQuality(Input, Settings);
}
void UProject_JCharacterAnimInstance::ApplyOptimizationPolicy(const FProject_JAnimOptimizationPolicy& NewPolicy)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_FollowerTickPolicy);
	check(IsInGameThread());
	CurrentOptimizationPolicy = NewPolicy;
	USkeletalMeshComponent* Follower = GetRuntimeRetargetFollowerMesh();
	if (FallbackFollowerTickMesh.Get() != Follower || !Follower || !Follower->IsRegistered())
	{
		ReleaseFollowerTickOverride();
	}
	if (!Follower || !Follower->IsRegistered()) return;
	if (Follower->ComponentTags.Contains(TEXT("NoVisualBudget")) || Follower->IsSimulatingPhysics() || Follower->LeaderPoseComponent.IsValid() ||
		Project_J::Animation::UsesIndependentClothSimulation(*Follower))
	{
		ReleaseFollowerTickOverride();
		return;
	}
	// Actual allocation, rather than coordinator configuration, owns these controls.
	// The coordinator releases this lease before it captures a mesh's authored tick.
	if (Follower->IsUsingExternalTickRateControl())
	{
		ReleaseFollowerTickOverride(false);
		return;
	}
	if (!FallbackFollowerTickMesh.IsValid())
	{
		FallbackFollowerTickMesh = Follower;
		bSavedFollowerTick = Follower->IsComponentTickEnabled();
		bAppliedFollowerRetarget = NewPolicy.bEnableFollowerRetarget;
		Follower->SetComponentTickEnabled(bAppliedFollowerRetarget);
	}
	else if (bAppliedFollowerRetarget != NewPolicy.bEnableFollowerRetarget)
	{
		bAppliedFollowerRetarget = NewPolicy.bEnableFollowerRetarget;
		Follower->SetComponentTickEnabled(bAppliedFollowerRetarget);
	}
}
void UProject_JCharacterAnimInstance::ReleaseFollowerTickOverride(bool bRestoreTick)
{
	check(IsInGameThread());
	if (USkeletalMeshComponent* Mesh = FallbackFollowerTickMesh.Get(); bRestoreTick && Mesh &&
		!Mesh->IsUsingExternalTickRateControl() && Mesh->IsComponentTickEnabled() == bAppliedFollowerRetarget)
	{
		Mesh->SetComponentTickEnabled(bSavedFollowerTick);
	}
	FallbackFollowerTickMesh.Reset();
}

FProject_JAnimationBudgetSettings UProject_JCharacterAnimInstance::GetEffectiveAnimationBudgetSettings() const
{
 if(const auto* Profile=GetLocomotionProfile()) return Profile->GetResolvedAnimationBudgetSettings();
 return FProject_JAnimationBudgetSettings();
}
