#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Animation/AnimInstance.h"
#include "Project_JBaseCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Components/MeshComponent.h"
#include "Components/Project_JModularMeshComponent.h"
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

	static TAutoConsoleVariable<int32> CVarRemoteProxyMontageTimingOnly(
		TEXT("Project_J.Anim.RemoteProxyMontageTimingOnly"),
		1,
		TEXT("Keep montage timing on client simulated players whose visual source is visible and uses visibility-based pose updates. Full-pose requirements retain priority."),
		ECVF_Default);

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
    CachedCoordinator.Reset();
    CachedCoordinatorOwner.Reset();
    bCoordinatorLookupAttempted = false;
    const TStrongObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Keep(IsGarbageCollecting()?nullptr:this);
    RetireSharedPhysicsBounds();TGuardValue<bool> Changing(bChangingSharedBoundsSource,true);
    Super::OnRegister();RetireSharedPhysicsBounds();
}

UProject_JAnimationUpdateCoordinatorComponent* UProject_JBudgetedSkeletalMeshComponent::GetCachedAnimationUpdateCoordinator()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        CachedCoordinator.Reset();
        CachedCoordinatorOwner.Reset();
        bCoordinatorLookupAttempted = false;
        return nullptr;
    }
    if (CachedCoordinatorOwner.Get() != Owner || !bCoordinatorLookupAttempted || CachedCoordinator.IsStale())
    {
        CachedCoordinatorOwner = Owner;
        CachedCoordinator.Reset();
        Owner->ForEachComponent<UProject_JAnimationUpdateCoordinatorComponent>(false,
            [this](UProject_JAnimationUpdateCoordinatorComponent* Candidate)
            {
                if (!CachedCoordinator.IsValid() && Candidate->IsRegistered() && !Candidate->IsBeingDestroyed())
                    CachedCoordinator = Candidate;
            });
        bCoordinatorLookupAttempted = true;
    }
    return CachedCoordinator.Get();
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

void UProject_JBudgetedSkeletalMeshComponent::TickPose(float DeltaTime, bool bNeedsValidRootMotion)
{
	Super::TickPose(DeltaTime, bNeedsValidRootMotion);
	if (!bEnding && OnPostTickPose.IsBound())
	{
		OnPostTickPose.Broadcast();
	}
}

void UProject_JBudgetedSkeletalMeshComponent::SetHiddenLeaderLateTick(bool bLate)
{
	if (bLate && !HiddenLeaderSavedTickGroup.IsSet())
	{
		HiddenLeaderSavedTickGroup = PrimaryComponentTick.TickGroup;
		SetTickGroup(TG_PostUpdateWork);
	}
	else if (!bLate && HiddenLeaderSavedTickGroup.IsSet())
	{
		SetTickGroup(HiddenLeaderSavedTickGroup.GetValue());
		HiddenLeaderSavedTickGroup.Reset();
	}
}

bool UProject_JBudgetedSkeletalMeshComponent::IsHiddenMontageOnlySafe(bool bAfterCamera)
{
	if (!bAfterCamera || !Service.IsValid() || IsSimulatingPhysics() || LeaderPoseComponent.IsValid()) return false;
	const auto* Coordinator = GetCachedAnimationUpdateCoordinator();
	if (!Coordinator || Coordinator->HasUrgentRemoteAnimationUpdate()) return false;

	bool bHasFollower = false;
	const auto IsDemanding = [this](const UMeshComponent& Part)
	{
		return Part.WasRecentlyRendered(.25f) || !Service->IsOutsideEveryLocalView(Part.Bounds);
	};
	// LeaderPose consumers may be attached to a sibling instead of the source.
	for (const auto* Component : GetOwner()->GetComponents())
	{
		const auto* Part = Cast<USkinnedMeshComponent>(Component);
		if (!IsValid(Part) || Part->LeaderPoseComponent.Get() != this || !Part->IsRegistered()
			|| !Part->IsVisible() || Part->bHiddenInGame || !Part->GetSkinnedAsset()) continue;
		// The managed modular parts and native cloth have source prerequisites.
		// An unknown consumer can run before the late source: keep full pose.
		if (!Part->IsA<UProject_JModularMeshComponent>() || IsDemanding(*Part)) return false;
		bHasFollower = true;
	}
	// Include weapons and other mesh children (including ChildActorComponent
	// descendants) whose socket position is driven by this pose or a follower.
	TArray<const USceneComponent*, TInlineAllocator<24>> Children;
	Children.Add(this);
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const auto* Parent = Children[Index];
		if (!IsValid(Parent) || !Parent->IsRegistered() || Children.Num() > 128) return false;
		for (const USceneComponent* Child : Parent->GetAttachChildren())
		{
			if (!IsValid(Child)) continue;
			Children.Add(Child);
			const auto* Part = Cast<UMeshComponent>(Child);
			if (Part && Part->IsRegistered() && Part->IsVisible() && !Part->bHiddenInGame && IsDemanding(*Part)) return false;
		}
	}
	return bHasFollower;
}

void UProject_JBudgetedSkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	if (HiddenLeaderSavedTickGroup.IsSet())
	{
		// UE updates local cameras before TG_PostUpdateWork. Use the actual
		// frame's tick group because a new montage can start during a tick.
		bHiddenLeaderOffscreen = IsHiddenMontageOnlySafe(ThisTickFunction
			&& ThisTickFunction->GetActualTickGroup() >= TG_PostUpdateWork);
		RefreshMontageUpdateRequirement();
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UProject_JBudgetedSkeletalMeshComponent::RefreshMontageUpdateRequirement()
{
	if (bEnding) { return; }
	const auto* Anim = GetAnimInstance();
	const bool bNeedsUpdate = Anim && Anim->IsAnyMontagePlaying();
	// Attack ownership may end before its montage blend-out. This independent
	// demand preserves offscreen evaluation and end notifies until all instances
	// drain, then composes with (rather than releasing) other callers' demands.
	// On dedicated servers, montage ticking/notifies do not need full skeletal bone evaluation
	// unless an explicit GameplayPose requester (e.g. non-baked live-socket attack) demands it.
	if (bNeedsUpdate)
	{
		const UWorld* World = GetWorld();
		const AProject_JPlayerCharacter* RemotePlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
		const auto Baseline = bVisibilityOverride ? OverrideSavedVisibility : VisibilityBasedAnimTickOption;
		const bool bRemoteProxy = World && World->GetNetMode() == NM_Client
			&& RemotePlayer && RemotePlayer->GetLocalRole() == ROLE_SimulatedProxy && !RemotePlayer->IsLocallyControlled()
			&& RemotePlayer->GetMesh() == this && bAllowProjectBudget
			&& Project_J::Anim::CVarRemoteProxyMontageTimingOnly.GetValueOnGameThread() != 0;
		const bool bVisualSource = IsVisible() && !bHiddenInGame;
		const auto* Coordinator = bRemoteProxy && !bVisualSource ? GetCachedAnimationUpdateCoordinator() : nullptr;
		const bool bLateHidden = Coordinator && !IsSimulatingPhysics() && !LeaderPoseComponent.IsValid()
			&& Anim->RootMotionMode != ERootMotionMode::RootMotionFromEverything
			&& Baseline == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		SetHiddenLeaderLateTick(bLateHidden);
		if (!bLateHidden || Coordinator->HasUrgentRemoteAnimationUpdate()) bHiddenLeaderOffscreen = false;
		const bool bVisibleSourceTiming = bRemoteProxy && bVisualSource
			&& (Baseline == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered
				|| Baseline == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered);
		const EProject_JAnimationUpdateRequirement Requirement =
			((World && World->GetNetMode() == NM_DedicatedServer) || bVisibleSourceTiming || bHiddenLeaderOffscreen)
				? EProject_JAnimationUpdateRequirement::MontageTiming
				: EProject_JAnimationUpdateRequirement::GameplayPose;
		if (!bMontageUpdateRequirement || UpdateRequirements.FindRef(this) != Requirement)
		{
			bMontageUpdateRequirement = true;
			RequestAnimationUpdate(this, Requirement);
		}
	}
	else if (bMontageUpdateRequirement)
	{
		bHiddenLeaderOffscreen = false;
		SetHiddenLeaderLateTick(false);
		bMontageUpdateRequirement = false;
		ReleaseAnimationUpdate(this);
	}
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
	bool bNeedsMontageTiming = false;
	for (auto It = UpdateRequirements.CreateIterator(); It; ++It)
	{
		if (!Project_J::Anim::IsUpdateRequesterAlive(It.Key().Get())) { It.RemoveCurrent(); }
		else
		{
			bNeedsGameplayPose |= It.Value() == EProject_JAnimationUpdateRequirement::GameplayPose;
			bNeedsMontageTiming |= It.Value() == EProject_JAnimationUpdateRequirement::MontageTiming;
		}
	}
	const bool bNeedsNotifies = bNeedsGameplayPose || bNeedsMontageTiming;
	const bool bNeedsVisibility = bNeedsGameplayPose || bNeedsMontageTiming;
	const bool bNeedsUpdate = !UpdateRequirements.IsEmpty();
	if (bNeedsUpdate && !bUpdateOverride)
	{
		// ABA must return its saved policy before we capture the baseline.
		LeaveBudget();
		bOverrideSavedURO = bEnableUpdateRateOptimizations;
	}
	if (bNeedsVisibility && !bVisibilityOverride)
	{
		OverrideSavedVisibility = VisibilityBasedAnimTickOption;
	}
	if (bNeedsNotifies && !bNotifyOverride)
	{
		bOverrideSavedSuppressNotifies = bSuppressNotifyEventDispatch;
	}
	if (bNeedsGameplayPose)
	{
		VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	else if (bNeedsMontageTiming)
	{
		if (bGameplayPoseOverride && VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			VisibilityBasedAnimTickOption = OverrideSavedVisibility;
		}
		if (VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered || bHiddenLeaderOffscreen)
		{
			VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
		}
	}
	else if (bVisibilityOverride)
	{
		// Respect newer explicit policies where the effective value differs.
		if ((bGameplayPoseOverride && VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
			|| (bMontageTimingOverride && VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered))
		{
			VisibilityBasedAnimTickOption = OverrideSavedVisibility;
		}
	}
	if (bNeedsNotifies) { bSuppressNotifyEventDispatch = false; }
	else if (bNotifyOverride && !bSuppressNotifyEventDispatch) { bSuppressNotifyEventDispatch = bOverrideSavedSuppressNotifies; }
	if (bNeedsUpdate) { bEnableUpdateRateOptimizations = false; }
	else if (bUpdateOverride && !bEnableUpdateRateOptimizations) { bEnableUpdateRateOptimizations = bOverrideSavedURO; }
	bGameplayPoseOverride = bNeedsGameplayPose;
	bMontageTimingOverride = bNeedsMontageTiming;
	bVisibilityOverride = bNeedsVisibility;
	bNotifyOverride = bNeedsNotifies;
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
	OnPostTickPose.Clear();
	ObservedDistanceTier.Reset(); ObservedFullClothQuality.Reset();
	LeaveBudget();
	UpdateRequirements.Reset();
	bMontageUpdateRequirement = false;
	bHiddenLeaderOffscreen = false;
	SetHiddenLeaderLateTick(false);
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
