#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Project_JBaseCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "IAnimationBudgetAllocator.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/IConsoleManager.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace Project_J::Anim
{
	static bool IsUpdateRequesterAlive(UObject* Requester)
	{
		if (!IsValid(Requester)) { return false; }
		if (const auto* Component = Cast<UActorComponent>(Requester))
		{
			const auto* Owner = Component->GetOwner();
			return !Component->IsBeingDestroyed() && IsValid(Owner) && !Owner->IsActorBeingDestroyed();
		}
		if (const auto* Actor = Cast<AActor>(Requester)) { return !Actor->IsActorBeingDestroyed(); }
		return true;
	}
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


bool UProject_JBudgetedSkeletalMeshComponent::IsSharedPhysicsBoundsAvailable() const
{
    const auto* World=GetWorld();const auto* Owner=GetOwner();
    return IsInGameThread() && !IsGarbageCollecting() && !bEnding && !bChangingSharedBoundsSource && !IsTemplate() &&
        IsRegistered() && AreBoneTransformsValid() && !IsBeingDestroyed() && IsValid(World) && !World->bIsTearingDown &&
        IsValid(Owner) && !Owner->IsActorBeingDestroyed();
}
bool UProject_JBudgetedSkeletalMeshComponent::TryReadSharedPhysicsBounds(const Project_J::Bounds::FKey& Key,
    TConstArrayView<Project_J::Bounds::FBodyValue> BodyValues,FBoxSphereBounds& OutLocal) const
{
    if(!IsSharedPhysicsBoundsAvailable() || Key.Frame!=GFrameCounter || Key.Source!=FObjectKey(this) ||
        Key.Owner!=FObjectKey(GetOwner()) || Key.World!=FObjectKey(GetWorld()) ||
        Key.SourceAsset!=FObjectKey(GetSkeletalMeshAsset()) || Key.PhysicsAsset!=FObjectKey(GetPhysicsAsset())) return false;
    if(!SharedPhysicsBounds.bValid || !(Key==SharedPhysicsBounds.Key) ||
        !Project_J::Bounds::SameBodies(BodyValues,SharedPhysicsBounds.Bodies)) return false;
    OutLocal=SharedPhysicsBounds.LocalBounds;
    return true;
}
void UProject_JBudgetedSkeletalMeshComponent::PublishSharedPhysicsBounds(const Project_J::Bounds::FKey& Key,
    TConstArrayView<Project_J::Bounds::FBodyValue> BodyValues,const FBoxSphereBounds& Local) const
{
    if(!IsSharedPhysicsBoundsAvailable() || BodyValues.Num()>Project_J::Bounds::MaxSharedBodies || Key.Frame!=GFrameCounter ||
        Key.Source!=FObjectKey(this) || Key.Owner!=FObjectKey(GetOwner()) || Key.World!=FObjectKey(GetWorld()) ||
        Key.SourceAsset!=FObjectKey(GetSkeletalMeshAsset()) || Key.PhysicsAsset!=FObjectKey(GetPhysicsAsset())) return;
    SharedPhysicsBounds.bValid=false;
    SharedPhysicsBounds.Key=Key;
    SharedPhysicsBounds.Bodies.Reset(); // Preserve steady-state allocation, never more than64 values.
    SharedPhysicsBounds.Bodies.Append(BodyValues.GetData(),BodyValues.Num());
    SharedPhysicsBounds.LocalBounds=Local;
    SharedPhysicsBounds.bValid=true;
}
void UProject_JBudgetedSkeletalMeshComponent::RetireSharedPhysicsBounds()
{
    SharedPhysicsBounds.bValid=false;
    SharedPhysicsBounds.Bodies.Empty(); // Lifecycle retirement releases the one-entry value allocation.
}
void UProject_JBudgetedSkeletalMeshComponent::OnRegister()
{
    const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Keep(IsGarbageCollecting()?nullptr:this);
    RetireSharedPhysicsBounds();TGuardValue<bool> Changing(bChangingSharedBoundsSource,true);
    Super::OnRegister();RetireSharedPhysicsBounds();
}
void UProject_JBudgetedSkeletalMeshComponent::OnUnregister()
{
    const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Keep(IsGarbageCollecting()?nullptr:this);
    RetireSharedPhysicsBounds();TGuardValue<bool> Changing(bChangingSharedBoundsSource,true);
    Super::OnUnregister();RetireSharedPhysicsBounds();
}
void UProject_JBudgetedSkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* NewMesh,bool bReinitPose)
{
    const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Keep(IsGarbageCollecting()?nullptr:this);
    RetireSharedPhysicsBounds();TGuardValue<bool> Changing(bChangingSharedBoundsSource,true);
    Super::SetSkeletalMesh(NewMesh,bReinitPose);RetireSharedPhysicsBounds();
}
void UProject_JBudgetedSkeletalMeshComponent::SetPhysicsAsset(UPhysicsAsset* NewAsset,bool bForceReInit)
{
    const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Keep(IsGarbageCollecting()?nullptr:this);
    RetireSharedPhysicsBounds();TGuardValue<bool> Changing(bChangingSharedBoundsSource,true);
    Super::SetPhysicsAsset(NewAsset,bForceReInit);RetireSharedPhysicsBounds();
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
    RetireSharedPhysicsBounds();
	ObservedDistanceTier.Reset(); ObservedFullClothQuality.Reset();
	LeaveBudget(); // Reinitialization cannot retain old external tick-rate state.
	if (BoundAnimation.IsValid()) { BoundAnimation->OnMontageStarted.RemoveDynamic(this, &ThisClass::OnMontageStarted); }
	BoundAnimation = GetAnimInstance();
	if (BoundAnimation.IsValid()) { BoundAnimation->OnMontageStarted.AddUniqueDynamic(this, &ThisClass::OnMontageStarted); }
	RefreshMontageUpdateRequirement();
}

float UProject_JBudgetedSkeletalMeshComponent::GetAnimationDistanceTier() const
{
	check(IsInGameThread());
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (ProjectJ::Animation::IsLocalPlayerAvatar(Pawn)) return 0.f;
	// No local view (including isolated fixtures and dedicated servers) retains
	// the existing caller-provided tier without creating a second global writer.
	const auto* Character = Cast<AProject_JBaseCharacter>(GetOwner());
	const float Tier = ObservedDistanceTier.Get(Character ? Character->GetSignificance() : 0.f);
	return FMath::IsFinite(Tier) ? FMath::Max(0.f, Tier) : 0.f;
}

void UProject_JBudgetedSkeletalMeshComponent::OnMontageStarted(UAnimMontage*)
{
	LeaveBudget();
	RefreshMontageUpdateRequirement();
}

void UProject_JBudgetedSkeletalMeshComponent::RefreshMontageUpdateRequirement()
{
	if (bEnding) { return; }
	const auto* Anim = GetAnimInstance();
	const bool bNeedsUpdate = Anim && Anim->IsAnyMontagePlaying();
	if (bNeedsUpdate == bMontageUpdateRequirement) { return; }
	bMontageUpdateRequirement = bNeedsUpdate;
	// Attack ownership may end before its montage blend-out. This independent
	// demand preserves offscreen evaluation and end notifies until all instances
	// drain, then composes with (rather than releasing) other callers' demands.
	if (bNeedsUpdate) { RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::GameplayPose); }
	else { ReleaseAnimationUpdate(this); }
}

bool UProject_JBudgetedSkeletalMeshComponent::CanUseBudget() const
{
	const auto* World = GetWorld();
	const auto* Pawn = Cast<APawn>(GetOwner());
	const auto* Anim = GetAnimInstance();
	const bool bLocalPlayer = ProjectJ::Animation::IsLocalPlayerAvatar(Pawn);
	return !bEnding && bAllowProjectBudget && !IsCombatCritical() && !bUpdateOverride && bRequestedTick && IsRegistered() && HasBegunPlay()
		// ABA enables external control itself. A pre-existing foreign controller
		// must retain ownership; do not reject our own active ABA registration.
		&& (bManaged || !IsUsingExternalTickRateControl())
		&& World && World->IsGameWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_DedicatedServer
		&& GetOwner() && !GetOwner()->IsActorBeingDestroyed() && !bLocalPlayer && !IsSimulatingPhysics()
		&& !LeaderPoseComponent.IsValid() && Anim && Anim->RootMotionMode != ERootMotionMode::RootMotionFromEverything
		// Stopped/blending-out instances still need animation ticks to dispatch
		// their end callbacks and release montage ownership. ABA can completely
		// skip an offscreen mesh, so rejoin only after those instances are drained.
		&& !Anim->IsAnyMontagePlaying();
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
	if (bEnding || !Project_J::Anim::IsUpdateRequesterAlive(Requester)) { return; }
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
	// The world policy pass also retires weak requesters. Idle meshes do no map
	// traversal or policy writes; no new component tick or expiry timer is needed.
	if (UpdateRequirements.IsEmpty() && !bUpdateOverride) { return; }
	bool bNeedsGameplayPose = false;
	for (auto It = UpdateRequirements.CreateIterator(); It; ++It)
	{
		if (!Project_J::Anim::IsUpdateRequesterAlive(It.Key().Get())) { It.RemoveCurrent(); }
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
    RetireSharedPhysicsBounds();
	bEnding = true;
	ObservedDistanceTier.Reset(); ObservedFullClothQuality.Reset();
	LeaveBudget();
	UpdateRequirements.Reset();
	bMontageUpdateRequirement = false;
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
