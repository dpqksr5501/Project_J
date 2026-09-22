#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "IAnimationBudgetAllocator.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"

namespace Project_J::Anim
{
	static TAutoConsoleVariable<int32> CVarBudgetTickWhenNotRendered(
		TEXT("Project_J.Anim.BudgetTickWhenNotRendered"),
		1,
		TEXT("Policy A (1): bBudgetTickWhenNotRendered=true (keeps Hidden Leader ticked by ABA).\n")
		TEXT("Policy B (0): bBudgetTickWhenNotRendered=false (skips off-screen non-combat characters, wakes only on GameplayPose).\n"),
		ECVF_Scalability);

	bool GetBudgetTickWhenNotRendered()
	{
		return CVarBudgetTickWhenNotRendered.GetValueOnGameThread() != 0;
	}
}

UProject_JBudgetedSkeletalMeshComponent::UProject_JBudgetedSkeletalMeshComponent(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	SetAutoRegisterWithBudgetAllocator(false);
	SetAutoCalculateSignificance(false);
	SetShouldUseActorRenderedFlag(true);
	bBudgetTickWhenNotRendered = true;
}

void UProject_JBudgetedSkeletalMeshComponent::BeginPlay()
{
	bEnding = false;
	// Only the project service owns registration, including when a BP overrides defaults.
	SetAutoRegisterWithBudgetAllocator(false);
	Super::BeginPlay();
	bBudgetTickWhenNotRendered = Project_J::Anim::CVarBudgetTickWhenNotRendered.GetValueOnGameThread() != 0;
	bRequestedTick = IsComponentTickEnabled();
	OnAnimInitialized.AddUniqueDynamic(this, &ThisClass::BindAnimationEvents);
	BindAnimationEvents();
	if (auto* World = GetWorld())
	{
		Service = World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>();
		if (Service.IsValid()) { Service->RegisterMesh(this); }
	}
}

void UProject_JBudgetedSkeletalMeshComponent::BindAnimationEvents()
{
	LeaveBudget(); // Reinitialization cannot retain old external tick-rate state.
	if (BoundAnimation.IsValid()) { BoundAnimation->OnMontageStarted.RemoveDynamic(this, &ThisClass::OnMontageStarted); }
	BoundAnimation = GetAnimInstance();
	if (BoundAnimation.IsValid()) { BoundAnimation->OnMontageStarted.AddUniqueDynamic(this, &ThisClass::OnMontageStarted); }
}

void UProject_JBudgetedSkeletalMeshComponent::OnMontageStarted(UAnimMontage*) { LeaveBudget(); }

bool UProject_JBudgetedSkeletalMeshComponent::CanUseBudget() const
{
	const auto* World = GetWorld();
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* Anim = GetAnimInstance();
	const bool bLocalPlayer = Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	return !bEnding && bAllowProjectBudget && !IsCombatCritical() && !bUpdateOverride && bRequestedTick && IsRegistered() && HasBegunPlay()
		&& World && World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer
		&& GetOwner() && !GetOwner()->IsActorBeingDestroyed() && !bLocalPlayer && !IsSimulatingPhysics()
		&& !LeaderPoseComponent.IsValid() && Anim && Anim->RootMotionMode != ERootMotionMode::RootMotionFromEverything
		&& !Anim->Montage_IsPlaying(nullptr);
}

void UProject_JBudgetedSkeletalMeshComponent::EnterBudget()
{
	if (bManaged || !CanUseBudget()) { return; }
	if (auto* Allocator = IAnimationBudgetAllocator::Get(GetWorld()))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CharacterABA_Register);
		bSavedURO = bEnableUpdateRateOptimizations;
		Allocator->RegisterComponent(this);
		bManaged = true;
	}
}

void UProject_JBudgetedSkeletalMeshComponent::LeaveBudget()
{
	if (!bManaged) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CharacterABA_Unregister);
	if (auto* World = GetWorld())
	{
		if (auto* Allocator = IAnimationBudgetAllocator::Get(World)) { Allocator->UnregisterComponent(this); }
	}
	bManaged = false;
	bEnableUpdateRateOptimizations = bSavedURO;
	PrimaryComponentTick.SetTickFunctionEnable(bRequestedTick);
}

void UProject_JBudgetedSkeletalMeshComponent::SetCombatCritical(bool bCritical)
{
	check(IsInGameThread());
	bCombatCritical = bCritical;
	if (bCritical) { LeaveBudget(); }
	// Rejoin only in the next policy pass, after all end/montage callbacks complete.
}

void UProject_JBudgetedSkeletalMeshComponent::RequestAnimationUpdate(UObject* Requester, EProject_JAnimationUpdateRequirement Requirement)
{
	check(IsInGameThread());
	if (bEnding || !IsValid(Requester)) { return; }
	UpdateRequirements.Add(Requester, Requirement);
	RefreshAnimationUpdateRequirements();
}

void UProject_JBudgetedSkeletalMeshComponent::ReleaseAnimationUpdate(UObject* Requester)
{
	check(IsInGameThread());
	UpdateRequirements.Remove(Requester);
	RefreshAnimationUpdateRequirements();
}

void UProject_JBudgetedSkeletalMeshComponent::RefreshAnimationUpdateRequirements()
{
	bool bNeedsGameplayPose = false;
	for (auto It = UpdateRequirements.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); }
		else { bNeedsGameplayPose |= It.Value() == EProject_JAnimationUpdateRequirement::GameplayPose; }
	}
	const bool bNeedsUpdate = !UpdateRequirements.IsEmpty();
	if (bNeedsUpdate && !bUpdateOverride)
	{
		// ABA must return its saved policy before we capture the baseline.
		LeaveBudget();
		bOverrideSavedURO = bEnableUpdateRateOptimizations;
	}
	if (bNeedsGameplayPose && !bGameplayPoseOverride)
	{
		OverrideSavedVisibility = VisibilityBasedAnimTickOption;
		bOverrideSavedSuppressNotifies = bSuppressNotifyEventDispatch;
	}
	if (bNeedsGameplayPose)
	{
		VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		bSuppressNotifyEventDispatch = false;
	}
	else if (bGameplayPoseOverride)
	{
		// Respect newer explicit policies where the effective value differs.
		if (VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			VisibilityBasedAnimTickOption = OverrideSavedVisibility;
		}
		if (!bSuppressNotifyEventDispatch) { bSuppressNotifyEventDispatch = bOverrideSavedSuppressNotifies; }
	}
	if (bNeedsUpdate) { bEnableUpdateRateOptimizations = false; }
	else if (bUpdateOverride && !bEnableUpdateRateOptimizations) { bEnableUpdateRateOptimizations = bOverrideSavedURO; }
	bGameplayPoseOverride = bNeedsGameplayPose;
	bUpdateOverride = bNeedsUpdate;
}

void UProject_JBudgetedSkeletalMeshComponent::SetComponentTickEnabled(bool bEnabled)
{
	bRequestedTick = bEnabled;
	Super::SetComponentTickEnabled(bEnabled);
}

void UProject_JBudgetedSkeletalMeshComponent::DetachService()
{
	bEnding = true;
	LeaveBudget();
	UpdateRequirements.Reset();
	RefreshAnimationUpdateRequirements();
	if (Service.IsValid()) { Service->UnregisterMesh(this); }
	Service.Reset();
	OnAnimInitialized.RemoveDynamic(this, &ThisClass::BindAnimationEvents);
	if (BoundAnimation.IsValid()) { BoundAnimation->OnMontageStarted.RemoveDynamic(this, &ThisClass::OnMontageStarted); }
	BoundAnimation.Reset();
}
void UProject_JBudgetedSkeletalMeshComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	DetachService();
	Super::EndPlay(Reason);
}
void UProject_JBudgetedSkeletalMeshComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	DetachService();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
