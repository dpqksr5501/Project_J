#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Project_JBaseCharacter.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "TimerManager.h"

UProject_JAnimationUpdateCoordinatorComponent::UProject_JAnimationUpdateCoordinatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(false);
}

void UProject_JAnimationUpdateCoordinatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreVisualBudget();
	RestoreRemoteAnimationUpdateRateOptimization();
	Super::EndPlay(EndPlayReason);
}

void UProject_JAnimationUpdateCoordinatorComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RestoreVisualBudget();
	RestoreRemoteAnimationUpdateRateOptimization();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JAnimationUpdateCoordinatorComponent::RequestUrgentRemoteAnimationUpdate(
	float DurationSeconds)
{
	if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f)
	{
		return;
	}
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = CharacterOwner->GetMesh();
	UWorld* World = GetWorld();
	if (!MeshComponent || !World)
	{
		return;
	}

	// Release the exact mesh we changed if an avatar replaces its mesh mid-window.
	if (bUrgentAnimationUpdateActive && OverriddenMesh.Get() != MeshComponent)
	{
		RestoreRemoteAnimationUpdateRateOptimization();
	}
	const float RemainingDuration = World->GetTimerManager().GetTimerRemaining(RestoreAnimationUpdateRateTimer);
	if (!bUrgentAnimationUpdateActive)
	{
		OverriddenMesh = MeshComponent;
		bRestoreAnimationUpdateRateOptimization = MeshComponent->bEnableUpdateRateOptimizations;
	}

	// Player meshes intentionally use URO for MMO scalability. Sparse replicated
	// one-shot boundaries briefly take presentation priority, then restore the
	// exact mesh policy that was active before the first overlapping request.
	bUrgentAnimationUpdateActive = true;
	if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(MeshComponent))
	{
		Budgeted->RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::Presentation);
	}
	else { MeshComponent->bEnableUpdateRateOptimizations = false; }

	// Requests compose by their latest expiry. A short landing/jump event must
	// never shorten the presentation window already granted to another event.
	const float UrgentUpdateDuration = FMath::Max(DurationSeconds, RemainingDuration);

	World->GetTimerManager().SetTimer(
		RestoreAnimationUpdateRateTimer,
		this,
		&UProject_JAnimationUpdateCoordinatorComponent::RestoreRemoteAnimationUpdateRateOptimization,
		UrgentUpdateDuration,
		false);
}

void UProject_JAnimationUpdateCoordinatorComponent::RestoreRemoteAnimationUpdateRateOptimization()
{
	if (!bUrgentAnimationUpdateActive)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RestoreAnimationUpdateRateTimer);
	}

	USkeletalMeshComponent* MeshComponent = OverriddenMesh.Get();
	if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(MeshComponent))
	{
		if (!bVisualWakeActive || VisualSource.Get() != MeshComponent) Budgeted->ReleaseAnimationUpdate(this);
	}
	else if (MeshComponent)
	{
		MeshComponent->bEnableUpdateRateOptimizations = bRestoreAnimationUpdateRateOptimization;
	}

	bUrgentAnimationUpdateActive = false;
	OverriddenMesh.Reset();
}

namespace
{
TAutoConsoleVariable<int32> VisualBudgetEnabled(TEXT("ProjectJ.VisualAnimationBudget.Enabled"), 1,
	TEXT("Coordinate character retarget/cosmetic mesh evaluation. 0 restores authored mesh controls."));
}

bool UProject_JAnimationUpdateCoordinatorComponent::IsVisualBudgetEnabled() const
{ return bAllowVisualBudget && VisualBudgetEnabled.GetValueOnGameThread() != 0; }

void UProject_JAnimationUpdateCoordinatorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) { SetComponentTickEnabled(false); return; }
	if (const auto* Character = Cast<ACharacter>(GetOwner()))
	{
		VisualSource = Character->GetMesh();
		AddTickPrerequisiteActor(GetOwner());
	}
}

void UProject_JAnimationUpdateCoordinatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick)
{
	Super::TickComponent(DeltaTime, TickType, Tick);
	RefreshVisualBudget(DeltaTime);
}

void UProject_JAnimationUpdateCoordinatorComponent::DiscoverVisualMeshes()
{
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(GetOwner());
	for (auto* Mesh : Meshes)
	{
		if (Mesh == VisualSource.Get() || !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset() ||
			Mesh->IsA<UProject_JModularMeshComponent>() || Mesh->LeaderPoseComponent.IsValid() || Mesh->IsSimulatingPhysics() ||
			Mesh->ComponentTags.Contains(TEXT("NoVisualBudget")) ||
			(Mesh->GetAnimInstance() && Mesh->GetAnimInstance()->RootMotionMode == ERootMotionMode::RootMotionFromEverything) ||
			(!Mesh->GetAnimInstance() && !Mesh->GetPostProcessInstance())) continue;
		if (Visuals.ContainsByPredicate([Mesh](const FVisualMesh& Entry) { return Entry.Mesh.Get() == Mesh; })) continue;
		if (Mesh->IsUsingExternalTickRateControl()) continue; // A different allocator owns it.
		if (auto* Native = Cast<UProject_JCharacterAnimInstance>(VisualSource->GetAnimInstance());
			Native && Native->FallbackFollowerTickMesh.Get() == Mesh)
		{
			// Capture authored controls, not the source fallback's hidden-policy override.
			Native->ReleaseFollowerTickOverride();
		}
		FVisualMesh Entry; Entry.Mesh = Mesh; Entry.bSavedURO = Mesh->bEnableUpdateRateOptimizations;
		Entry.bSavedTick = Mesh->IsComponentTickEnabled(); Entry.SavedVisibility = Mesh->VisibilityBasedAnimTickOption;
		Entry.bAddedSourcePrerequisite = !Mesh->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
			[this](const FTickPrerequisite& Prerequisite) { return Prerequisite.PrerequisiteObject.Get() == VisualSource.Get(); });
		if (Entry.bAddedSourcePrerequisite) Mesh->AddTickPrerequisiteComponent(VisualSource.Get());
		Visuals.Add(Entry); Mesh->AddTickPrerequisiteComponent(this);
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->EnableExternalTickRateControl(true);
		// Group visibility, not an independently hidden source/garment timestamp, owns evaluation.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void UProject_JAnimationUpdateCoordinatorComponent::FVisualMesh::Restore(UActorComponent* Coordinator, USkeletalMeshComponent* Source)
{
	if (auto* Component = Mesh.Get())
	{
		Component->EnableExternalTickRateControl(false); Component->EnableExternalInterpolation(false);
		Component->EnableExternalEvaluationRateLimiting(false); Component->EnableExternalUpdate(true);
		Component->bEnableUpdateRateOptimizations = bSavedURO;
		if (Component->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
			Component->VisibilityBasedAnimTickOption = SavedVisibility;
		if (bDisabledTick) Component->SetComponentTickEnabled(bSavedTick);
		Component->RemoveTickPrerequisiteComponent(Coordinator);
		if (bAddedSourcePrerequisite && Source) Component->RemoveTickPrerequisiteComponent(Source);
	}
}

void UProject_JAnimationUpdateCoordinatorComponent::RestoreVisualBudget()
{
	for (auto& Entry : Visuals) Entry.Restore(this, VisualSource.Get());
	Visuals.Reset(); VisualSchedule = {}; AccumulatedVisualDelta = VisualInterval = 0;
	bVisualHidden = bSawAuthoredPresentation = false; AuthoredReturnUntil = 0; DiscoveryElapsed = .5f;
	if (bVisualWakeActive)
	{
		bVisualWakeActive = false;
		if (auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(VisualSource.Get()))
			if (!bUrgentAnimationUpdateActive || OverriddenMesh != VisualSource) Mesh->ReleaseAnimationUpdate(this);
	}
}

void UProject_JAnimationUpdateCoordinatorComponent::RefreshSourceRequirement()
{
	if (auto* Source = Cast<UProject_JBudgetedSkeletalMeshComponent>(VisualSource.Get()))
	{
		if ((bUrgentAnimationUpdateActive && OverriddenMesh == VisualSource) || bVisualWakeActive) Source->RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::Presentation);
		else Source->ReleaseAnimationUpdate(this);
	}
}

void UProject_JAnimationUpdateCoordinatorComponent::RefreshVisualBudget(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_VisualAnimationBudget);
	auto* Character = Cast<AProject_JBaseCharacter>(GetOwner());
	auto* Source = Character ? Cast<UProject_JBudgetedSkeletalMeshComponent>(Character->GetMesh()) : nullptr;
	auto* Native = Source ? Cast<UProject_JCharacterAnimInstance>(Source->GetAnimInstance()) : nullptr;
	if (!Source || !Native || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer ||
		!IsVisualBudgetEnabled() || Source->IsSimulatingPhysics())
	{ RestoreVisualBudget(); return; }
	if (VisualSource.Get() != Source)
	{
		RestoreVisualBudget(); VisualSource = Source;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (bVisualWakeActive && Now >= VisualWakeUntil) { bVisualWakeActive = false; RefreshSourceRequirement(); }
	DiscoveryElapsed += FMath::Max(0.f, DeltaTime);
	if (DiscoveryElapsed >= .5f) { DiscoverVisualMeshes(); DiscoveryElapsed = 0; }
	const bool bLocal = Cast<APlayerController>(Character->GetController()) && Character->IsLocallyControlled();
	const bool bAuthored = Native->NeedsFullRateVisualUpdate();
	if (bSawAuthoredPresentation && !bAuthored) AuthoredReturnUntil = Now + .2;
	bSawAuthoredPresentation = bAuthored;
	bool bProtected = bLocal || Source->IsCombatCritical() || Source->HasAnimationUpdateRequirement() ||
		Source->GetAnimInstance()->RootMotionMode == ERootMotionMode::RootMotionFromEverything ||
		Source->GetAnimInstance()->IsAnyMontagePlaying() || Character->GetCharacterMovement()->HasAnimRootMotion() ||
		Character->GetCharacterMovement()->CurrentRootMotion.HasActiveRootMotionSources() ||
		bAuthored || Now < AuthoredReturnUntil;
	for (const auto& Entry : Visuals) if (const auto* Mesh = Entry.Mesh.Get())
		bProtected |= Mesh->IsSimulatingPhysics() || (Mesh->GetAnimInstance() && Mesh->GetAnimInstance()->IsAnyMontagePlaying());
	const bool bHidden = !bProtected && !Project_J::Animation::WasCharacterVisualRecentlyRendered(*Character, .25f);
	const bool bWake = bVisualHidden && !bHidden;
	if (bWake)
	{
		bVisualWakeActive = true; VisualWakeUntil = Now + .15;
		RefreshSourceRequirement(); bProtected = true;
		for (const auto& Entry : Visuals) if (auto* Mesh = Entry.Mesh.Get())
		{
			if (auto* Anim = Mesh->GetAnimInstance()) Anim->ResetDynamics(ETeleportType::TeleportPhysics);
			if (auto* Anim = Mesh->GetPostProcessInstance()) Anim->ResetDynamics(ETeleportType::TeleportPhysics);
		}
	}
	const float DesiredInterval = ProjectJ::Animation::ResolveVisualInterval(bProtected, bHidden,
		Character->GetSignificance(), Source->IsManagedByBudget() && Source->GetExternalTickRate() > 1);
	// Upgrade immediately; downgrade only after a short quiet period. The schedule
	// phases periodic evaluation per owner so a 200-character arrival does not lockstep.
	if (DesiredInterval > VisualInterval)
	{
		if (DowngradeSince < 0) DowngradeSince = Now;
		if (Now - DowngradeSince >= .2) { VisualInterval = DesiredInterval; DowngradeSince = -1; }
	}
	else { VisualInterval = DesiredInterval; DowngradeSince = -1; }
	AccumulatedVisualDelta += FMath::Max(0.f, DeltaTime);
	bEvaluateVisuals = !bHidden && VisualSchedule.Advance(DeltaTime, VisualInterval, Character->GetUniqueID(), bWake || bProtected);
	const float EffectiveRate = DeltaTime > UE_SMALL_NUMBER ? FMath::Clamp(VisualInterval / DeltaTime, 1.f, 255.f) : 1.f;
	// Evaluation installs a new target pose. Start its blend at 1/rate, then
	// blend the current pose toward that target until the next evaluation.
	const float InterpolationElapsed = bEvaluateVisuals ? 0 : AccumulatedVisualDelta;
	const float Alpha = VisualInterval > 0 ? FMath::Clamp(DeltaTime / FMath::Max(VisualInterval - InterpolationElapsed, DeltaTime), 0.f, 1.f) : 1;
	for (int32 Index = Visuals.Num() - 1; Index >= 0; --Index)
	{
		auto& Entry = Visuals[Index]; auto* Mesh = Entry.Mesh.Get();
		if (!Mesh) { Visuals.RemoveAtSwap(Index); continue; }
		if (Mesh->LeaderPoseComponent.IsValid() || Mesh->IsSimulatingPhysics() || Mesh->ComponentTags.Contains(TEXT("NoVisualBudget")) ||
			!Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset() ||
			(Mesh->GetAnimInstance() && Mesh->GetAnimInstance()->RootMotionMode == ERootMotionMode::RootMotionFromEverything))
		{ Entry.Restore(this, VisualSource.Get()); Visuals.RemoveAtSwap(Index); continue; }
		const bool bDisable = bHidden || Mesh->bHiddenInGame || !Mesh->GetVisibleFlag();
		if (bDisable && !Entry.bDisabledTick) { Entry.bSavedTick = Mesh->IsComponentTickEnabled(); Mesh->SetComponentTickEnabled(false); Entry.bDisabledTick = true; }
		else if (!bDisable && Entry.bDisabledTick) { Mesh->SetComponentTickEnabled(Entry.bSavedTick); Entry.bDisabledTick = false; }
		Mesh->EnableExternalUpdate(bEvaluateVisuals);
		Mesh->EnableExternalInterpolation(VisualInterval > 0);
		Mesh->EnableExternalEvaluationRateLimiting(VisualInterval > 0);
		Mesh->SetExternalTickRate(FMath::CeilToInt(EffectiveRate));
		Mesh->SetExternalDeltaTime(AccumulatedVisualDelta); Mesh->SetExternalInterpolationAlpha(Alpha);
	}
	if (bEvaluateVisuals || bHidden) AccumulatedVisualDelta = 0;
	bVisualHidden = bHidden;
}
