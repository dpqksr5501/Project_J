#include "Components/Project_JCombatPresentationComponent.h"

#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraWorldManager.h"
#include "NiagaraComponentPool.h"
#include "Particles/ParticleSystemComponent.h"
#include "Project_JBaseCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatPresentation, Log, All);

namespace ProjectJCombatPresentationDebug
{
	static TAutoConsoleVariable<int32> CVarPool(
		TEXT("ProjectJ.Combat.Presentation.Pool"), 1,
		TEXT("Use engine Niagara pools for cosmetic cues. 0 restores unpooled creation for comparison."));

	// Match Niagara's locally viewed owner/instigator/attachment policy. Its
	// WorldManager helper is not exported by the installed engine module.
	static bool IsLocallyViewedEffect(const USceneComponent* Component)
	{
		for (const USceneComponent* Current = Component; IsValid(Current); Current = Current->GetAttachParent())
		{
			const AActor* Owner = Current->GetOwner();
			if (!IsValid(Owner)) { continue; }
			const APawn* OwnerPawn = Cast<APawn>(Owner);
			const APawn* Instigator = Owner->GetInstigator();
			if ((OwnerPawn && OwnerPawn->IsLocallyViewed()) || (IsValid(Instigator) && Instigator->IsLocallyViewed())) { return true; }
		}
		return false;
	}

	static void StopComponent(UNiagaraComponent* Component, bool bImmediate)
	{
		if (!IsValid(Component)) { return; }
		if (Component->PoolingMethod == ENCPoolMethod::ManualRelease)
		{
			// Manual ownership prevents a completed tracked loop from being recycled
			// under a different attack before this component releases its reference.
			if (bImmediate) { Component->DeactivateImmediate(); }
			Component->ReleaseToPool();
		}
		else if (bImmediate) { Component->DestroyComponent(); }
		else { Component->Deactivate(); }
	}
	static TAutoConsoleVariable<int32> CVarEnabled(
		TEXT("ProjectJ.Combat.Presentation.Debug"),
		0,
		TEXT("Logs the combat VFX cue resolution and Niagara spawn path. 0: off, 1: on."),
		ECVF_Default);

	static bool IsEnabled()
	{
		return CVarEnabled.GetValueOnGameThread() != 0;
	}
}

UProject_JCombatPresentationComponent::UProject_JCombatPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The owning client can lose its predicted attack state before a montage notify
	// runs. It must receive this tiny recovery state too, otherwise a dedicated
	// server has no local viewport in which to play the cue for that client.
	DOREPLIFETIME_CONDITION(UProject_JCombatPresentationComponent, ReplicatedPresentationState, COND_None);
}

void UProject_JCombatPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RetirePresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatPresentationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RetirePresentation();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JCombatPresentationComponent::BeginPlay()
{
	bEndingPresentation = false;
	Super::BeginPlay();
	// Initial replication can already have installed a loop before BeginPlay.
	// Warm current assets without retiring that accepted recovery generation.
	if (CanAcceptPresentation()) { PreloadVisualAssets(); }
}

bool UProject_JCombatPresentationComponent::CanAcceptPresentation() const
{
	return !bEndingPresentation && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown;
}

void UProject_JCombatPresentationComponent::RetirePresentation()
{
	if (bEndingPresentation) { return; }
	bEndingPresentation = true;
	ResetVisualAssetLeases();
	EndAttackPresentation();
}

void UProject_JCombatPresentationComponent::BeginAttackPresentation(const FGameplayTag AttackTag)
{
	if (!CanAcceptPresentation()) { return; }
    const bool bHadWarning = !ActiveWarningCues.IsEmpty() || !ReplicatedPresentationState.ActiveWarningCueTags.IsEmpty();
	if (!AttackTag.IsValid())
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] BeginAttack rejected: invalid AttackTag. Owner=%s"), *GetNameSafe(GetOwner()));
		}
		EndAttackPresentation();
		return;
	}

	// Each ability/node begin is a new instance, including repeated attacks with the same tag.
	{
		const uint32 Instance = ++ActiveAttackInstance;
		ActiveAttackTag = AttackTag;
		StopAllCues();
		if (!CanAcceptPresentation() || ActiveAttackInstance != Instance || ActiveAttackTag != AttackTag) { return; }
		if (UProject_JWeaponPresentationComponent* Weapon = GetOwner()->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Weapon->SetActiveAttackPresentation(AttackTag);
		}
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] BeginAttack Owner=%s Attack=%s Authority=%d"),
				*GetNameSafe(GetOwner()), *AttackTag.ToString(), GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
		}
		if (GetOwner() && GetOwner()->HasAuthority())
		{
			ReplicatedPresentationState.ActiveAttackTag = AttackTag;
			ReplicatedPresentationState.AttackInstance = ActiveAttackInstance;
			ReplicatedPresentationState.ActiveLoopingCueTags.Reset();
			ReplicatedPresentationState.ActiveWarningCueTags.Reset();
			PublishRecoveryState();
            if (bHadWarning) { MulticastWarningPresentationState(ReplicatedPresentationState); }
		}
	}
}

void UProject_JCombatPresentationComponent::EndAttackPresentation()
{
    const bool bHadWarning = !ActiveWarningCues.IsEmpty() || !ReplicatedPresentationState.ActiveWarningCueTags.IsEmpty();
	const bool bHadAttack = ActiveAttackTag.IsValid() || !ActiveLoopingCues.IsEmpty()
		|| ReplicatedPresentationState.ActiveAttackTag.IsValid();
	const uint32 Instance = ++ActiveAttackInstance;
	if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] EndAttack Owner=%s PreviousAttack=%s Authority=%d"),
			*GetNameSafe(GetOwner()), *ActiveAttackTag.ToString(), GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
	}
	ActiveAttackTag = FGameplayTag();
	StopAllCues();
	if (ActiveAttackInstance != Instance || ActiveAttackTag.IsValid()) { return; }
	if (AActor* Owner = GetOwner())
	{
		if (UProject_JWeaponPresentationComponent* Weapon = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Weapon->SetActiveAttackPresentation(FGameplayTag());
		}
	}
	ReplicatedPresentationState.ActiveAttackTag = FGameplayTag();
	ReplicatedPresentationState.ActiveLoopingCueTags.Reset();
	ReplicatedPresentationState.ActiveWarningCueTags.Reset();
	if (bHadAttack && CanAcceptPresentation() && GetOwner()->HasAuthority())
	{
		PublishRecoveryState();
        if (bHadWarning) { MulticastWarningPresentationState(ReplicatedPresentationState); }
		MulticastEndAttackPresentation(++NextPresentationEventOrder);
	}
}

void UProject_JCombatPresentationComponent::PlayCue(const FGameplayTag CueTag)
{
	if (!CanAcceptPresentation()) { return; }
	if (!ActiveAttackTag.IsValid())
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] PlayCue rejected: no active attack. Owner=%s Cue=%s"),
				*GetNameSafe(GetOwner()), *CueTag.ToString());
		}
		return;
	}

	const FProject_JCombatVFXCueDefinition* Cue = ResolveCue(CueTag);
	const bool bWarning = ActiveWarningCues.HasTagExact(CueTag);
	if ((!Cue || Cue->NiagaraSystem.IsNull()) && !bWarning)
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Cue resolution failed. Owner=%s Attack=%s Cue=%s"),
				*GetNameSafe(GetOwner()), *ActiveAttackTag.ToString(), *CueTag.ToString());
		}
		return;
	}
	if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] PlayCue Owner=%s Attack=%s Cue=%s System=%s Authority=%d"),
			*GetNameSafe(GetOwner()), *ActiveAttackTag.ToString(), *CueTag.ToString(), *(Cue ? Cue->NiagaraSystem.ToSoftObjectPath().ToString() : FString()),
			GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bool bRecoveryChanged = false;
        bool bWarningChanged = false;
		if (Cue && Cue->bLooping)
		{
			if (!ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(CueTag))
			{
				ReplicatedPresentationState.ActiveLoopingCueTags.AddTag(CueTag);
				bRecoveryChanged = true;
			}
		}

        if (bWarning && !ReplicatedPresentationState.ActiveWarningCueTags.HasTagExact(CueTag))
        {
            ReplicatedPresentationState.ActiveWarningCueTags.AddTag(CueTag);
            bRecoveryChanged = bWarningChanged = true;
        }
        if (bRecoveryChanged) { PublishRecoveryState(); }
        if (bWarningChanged) { MulticastWarningPresentationState(ReplicatedPresentationState); }

		// A listen server is both the authority and a local viewer.  The multicast
		// handler deliberately skips authority to avoid duplicate effects, so play
		// the cosmetic locally before notifying remote clients.
		PlayCueLocal(CueTag);
		MulticastPlayPresentationCue(ActiveAttackTag, CueTag, true, ++NextPresentationEventOrder, ActiveAttackInstance, ReplicatedPresentationState);
		return;
	}

	PlayCueLocal(CueTag);
}

bool UProject_JCombatPresentationComponent::ShouldBudgetRemoteSpawn(const FProject_JCombatVFXCueDefinition& Cue) const
{
	const auto* World = GetWorld(); const auto* Owner = GetOwner(); const auto* Pawn = Cast<APawn>(Owner);
	// Preview/editor worlds have no game-world budget. Preserve their immediate authoring path.
	return Cue.bUseRemoteSpawnBudget && World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE)
		&& IsValid(Owner) && !(Pawn && Pawn->IsLocallyControlled())
		&& !ProjectJCombatPresentationDebug::IsLocallyViewedEffect(Owner->GetRootComponent());
}
void UProject_JCombatPresentationComponent::PlayCueLocal(const FGameplayTag CueTag, bool bBudgetGranted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatPresentation_Spawn);
	if (!CanAcceptPresentation()) { return; }
	// Match Niagara creation admission before requesting a purely visual payload.
	// Authority loop/notify ownership and remote delivery remain in PlayCue.
	if (GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender())
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] Local spawn skipped: dedicated server or rendering unavailable. Cue=%s"), *CueTag.ToString());
		}
		return;
	}
	if (!ActiveAttackTag.IsValid())
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn skipped: no active attack. Cue=%s"), *CueTag.ToString());
		}
		return;
	}
	// Core attention is independent of optional profile/NS load, queue credits and Niagara scalability.
    if (ActiveWarningCues.HasTagExact(CueTag))
    {
        const uint32 Instance = ActiveAttackInstance;
        UpdateWarningMarker();
        if (!CanAcceptPresentation() || ActiveAttackInstance != Instance) { return; }
    }
	if (StartedCueTags.HasTagExact(CueTag) || ActiveLoopingCues.Contains(CueTag))
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] Local spawn skipped: cue already started for this attack. Cue=%s"), *CueTag.ToString());
		}
		return;
	}

	const FProject_JCombatVFXCueDefinition* Cue = ResolveCue(CueTag);
	if (!Cue || Cue->NiagaraSystem.IsNull())
	{
		if (!ActiveWarningCues.HasTagExact(CueTag) && ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: cue could not be resolved. Attack=%s Cue=%s"),
				*ActiveAttackTag.ToString(), *CueTag.ToString());
		}
		return;
	}


	// Copy authored values before async admission or Niagara callbacks. The array
	// storage and currently selected profile may be replaced by reentrant code.
	const FProject_JCombatVFXCueDefinition CueValues = *Cue;
	const FSoftObjectPath SystemPath = CueValues.NiagaraSystem.ToSoftObjectPath();
	const bool bRemoteBudget = ShouldBudgetRemoteSpawn(CueValues);
	if (const auto* Deferred = PendingLoopingCues.Find(CueTag);
		bRemoteBudget && !bBudgetGranted && Deferred && Deferred->bReadyForBudget && Deferred->AttackInstance == ActiveAttackInstance && Deferred->SystemPath == SystemPath)
	{
		// Recovery of this same desired loop cannot turn an admitted continuation
		// into an immediate warm spawn. Its one queued application consumes this flag.
		return;
	}
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	TStrongObjectPtr<UWorld> PinnedWorld(GetWorld());
	TStrongObjectPtr<UNiagaraSystem> PinnedSystem(CueValues.NiagaraSystem.Get());
	if (!PinnedSystem.IsValid())
	{
		if (CueValues.bLooping)
		{
			if (!PendingLoopingCues.Contains(CueTag) && PendingLoopingCues.Num() >= MaxVisualAssetPaths) { return; }
			PendingLoopingCues.Add(CueTag, { SystemPath, ActiveAttackInstance });
			if (!EnsureVisualAsset(SystemPath, true)) { PendingLoopingCues.Remove(CueTag); }
		}
		else
		{
			// The authored instant has passed. Warm subsequent uses, never replay
			// this one-shot late or once more when prediction meets the multicast.
			StartedCueTags.AddTag(CueTag);
			EnsureVisualAsset(SystemPath, true);
		}
		return;
	}
	if (CueValues.bLooping)
	{
		if (!PendingLoopingCues.Contains(CueTag) && PendingLoopingCues.Num() >= MaxVisualAssetPaths) { return; }
		// Keep the desired loop until spawn commits. StopCue called by a Niagara
		// callback can remove it and prevent this spawn from being published.
		PendingLoopingCues.Add(CueTag, { SystemPath, ActiveAttackInstance });
	}
	EnsureVisualAsset(SystemPath, true); // Cue demand may replace an unused warm lease.
	if (bRemoteBudget && !bBudgetGranted)
	{
		if (CueValues.bLooping)
		{
			PendingLoopingCues.FindChecked(CueTag).bReadyForBudget = true;
			QueueReadyLoopApplications(); return;
		}
		// The instant is consumed even when denied. A later multicast/recovery must
		// not replay this one-shot once a new frame has replenished the world's credits.
		StartedCueTags.AddTag(CueTag);
		auto* Budget = GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		if (!Budget || !Budget->TryAcquireOneShot(this)) { return; }
	}
	const uint32 SpawnRevision = VisualAssetRevision;
	const uint32 SpawnInstance = ActiveAttackInstance;
	const FGameplayTag SpawnAttack = ActiveAttackTag;
	USceneComponent* AttachComponent = nullptr;
	if (AProject_JBaseCharacter* Character = Cast<AProject_JBaseCharacter>(GetOwner()))
	{
		if (CueValues.AttachmentTarget == EProject_JCombatVFXAttachmentTarget::Weapon)
		{
			if (UProject_JWeaponPresentationComponent* WeaponPresentation = Character->FindComponentByClass<UProject_JWeaponPresentationComponent>())
			{
				AttachComponent = WeaponPresentation->GetWeaponVFXAttachmentComponent(CueValues.AttachSocketName);
			}
		}
		else if (CueValues.AttachmentTarget == EProject_JCombatVFXAttachmentTarget::CharacterMesh)
		{
			AttachComponent = Character->GetMesh();
		}
	}
	else if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: owner is not AProject_JBaseCharacter. Owner=%s"), *GetNameSafe(GetOwner()));
	}


	TStrongObjectPtr<USceneComponent> PinnedAttachment(AttachComponent);
	// Claim before invoking Niagara, so a callback cannot recursively spawn the
	// same cue. A replaced generation is responsible for its own new claim.
	StartedCueTags.AddTag(CueTag);
	UNiagaraComponent* NiagaraComponent = nullptr;
	const ENCPoolMethod Pool = ProjectJCombatPresentationDebug::CVarPool.GetValueOnGameThread()
		? (CueValues.bLooping ? ENCPoolMethod::ManualRelease : ENCPoolMethod::AutoRelease) : ENCPoolMethod::None;
	// World cues have no live attachment from which Niagara can infer linkage.
	// Attached cues retain Niagara's dynamic linkage when the view target changes.
	const bool bForceLocalPlayerEffect = CueValues.AttachmentTarget == EProject_JCombatVFXAttachmentTarget::World
		&& ProjectJCombatPresentationDebug::IsLocallyViewedEffect(PinnedOwner->GetRootComponent());
	FFXSystemSpawnParameters SpawnParams;
	SpawnParams.WorldContextObject = PinnedWorld.Get();
	SpawnParams.SystemTemplate = PinnedSystem.Get();
	SpawnParams.bAutoDestroy = true;
	SpawnParams.bAutoActivate = false;
	SpawnParams.PoolingMethod = ToPSCPoolMethod(Pool);
	SpawnParams.bPreCullCheck = true;
	SpawnParams.bIsPlayerEffect = bForceLocalPlayerEffect;
	if (CueValues.AttachmentTarget == EProject_JCombatVFXAttachmentTarget::World)
	{
		const FTransform SpawnTransform = CueValues.RelativeTransform * PinnedOwner->GetActorTransform();
		SpawnParams.Location = SpawnTransform.GetLocation();
		SpawnParams.Rotation = SpawnTransform.Rotator();
		SpawnParams.Scale = SpawnTransform.GetScale3D();
		NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocationWithParams(SpawnParams);
	}
	else if (AttachComponent)
	{
		SpawnParams.AttachToComponent = AttachComponent;
		SpawnParams.AttachPointName = CueValues.AttachSocketName;
		SpawnParams.Location = CueValues.RelativeTransform.GetLocation();
		SpawnParams.Rotation = CueValues.RelativeTransform.Rotator();
		SpawnParams.Scale = CueValues.RelativeTransform.GetScale3D();
		SpawnParams.LocationType = EAttachLocation::KeepRelativeOffset;
		NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(SpawnParams);
	}
	else if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: no attachment component. Target=%d Socket=%s Owner=%s"),
			static_cast<int32>(CueValues.AttachmentTarget), *CueValues.AttachSocketName.ToString(), *GetNameSafe(GetOwner()));
	}

	TStrongObjectPtr<UNiagaraComponent> PinnedCue(NiagaraComponent);
	const auto IsCurrentSpawn = [&]()
	{
		const FPendingLoop* Desired = PendingLoopingCues.Find(CueTag);
		return CanAcceptPresentation() && GetOwner() == PinnedOwner.Get() && GetWorld() == PinnedWorld.Get()
			&& VisualAssetRevision == SpawnRevision && ActiveAttackInstance == SpawnInstance && ActiveAttackTag == SpawnAttack
			&& (!CueValues.bLooping || (Desired && Desired->AttackInstance == SpawnInstance && Desired->SystemPath == SystemPath));
	};
	bool bActivationAttempted = false;
	if (IsValid(NiagaraComponent) && IsCurrentSpawn())
	{
		// Pool reuse does not reset this flag. Set both directions before any
		// activation; attached cues always clear force and use live attachment linkage.
		NiagaraComponent->SetForceLocalPlayerEffect(bForceLocalPlayerEffect);
		if (IsValid(NiagaraComponent) && IsCurrentSpawn())
		{
			bActivationAttempted = true;
			NiagaraComponent->Activate(true);
		}
	}

	if (!IsCurrentSpawn() || (NiagaraComponent && !IsValid(NiagaraComponent)))
	{
		if (CueValues.bLooping && VisualAssetRevision == SpawnRevision && ActiveAttackInstance == SpawnInstance)
		{
			StartedCueTags.RemoveTag(CueTag);
		}
		if (!bActivationAttempted && IsValid(NiagaraComponent) && !NiagaraComponent->IsActive()
			&& NiagaraComponent->PoolingMethod == ENCPoolMethod::AutoRelease)
		{
			// An unactivated AutoRelease cue is already fully complete, so
			// Deactivate cannot trigger its normal completion-based pool return.
			if (FNiagaraWorldManager* Manager = FNiagaraWorldManager::Get(PinnedWorld.Get()))
			{
				Manager->GetComponentPool()->ReclaimWorldParticleSystem(NiagaraComponent);
			}
			else { NiagaraComponent->DestroyComponent(); }
		}
		else if (IsValid(NiagaraComponent) && NiagaraComponent->PoolingMethod != ENCPoolMethod::FreeInPool)
		{
			ProjectJCombatPresentationDebug::StopComponent(NiagaraComponent, CueValues.bDestroyImmediatelyOnStop);
		}
		return;
	}
	if (CueValues.bLooping)
	{
		PendingLoopingCues.Remove(CueTag); StartedCueTags.RemoveTag(CueTag);
		if (PendingLoopingCues.IsEmpty()) { CancelReadyLoopApplications(); }
	}

	if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		if (NiagaraComponent)
		{
			UE_LOG(LogProjectJCombatPresentation, Log,
				TEXT("[CombatVFX] Niagara spawned. System=%s Target=%d Socket=%s AttachComponent=%s"),
				*CueValues.NiagaraSystem.ToSoftObjectPath().ToString(), static_cast<int32>(CueValues.AttachmentTarget),
				*CueValues.AttachSocketName.ToString(), *GetNameSafe(AttachComponent));
		}
		else
		{
			UE_LOG(LogProjectJCombatPresentation, Warning,
				TEXT("[CombatVFX] Niagara spawn returned null. System=%s Target=%d Socket=%s AttachComponent=%s"),
				*CueValues.NiagaraSystem.ToSoftObjectPath().ToString(), static_cast<int32>(CueValues.AttachmentTarget),
				*CueValues.AttachSocketName.ToString(), *GetNameSafe(AttachComponent));
		}
	}

	if (CueValues.bLooping && NiagaraComponent)
	{
		ActiveLoopingCues.Add(CueTag, NiagaraComponent);
		if (CueValues.bDestroyImmediatelyOnStop)
		{
			ImmediateDestroyCueTags.AddTag(CueTag);
		}
	}
	if (NiagaraComponent && !CueValues.bLooping)
	{
		StartedCueTags.AddTag(CueTag);
	}
}

void UProject_JCombatPresentationComponent::StopCue(const FGameplayTag CueTag)
{
	if (!CanAcceptPresentation()) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	TWeakObjectPtr<UWorld> OriginalWorld(GetWorld());
	for (auto It = CueLeases.CreateIterator(); It; ++It)
    {
        if (It.Value() == CueTag) { WarningCueLeases.Remove(It.Key()); It.RemoveCurrent(); }
    }
    EndWarningCue(CueTag);
    if (!CanAcceptPresentation()) { return; }
	if (PinnedOwner->HasAuthority())
	{
		const uint32 Instance = ActiveAttackInstance;
		const FGameplayTag Attack = ActiveAttackTag;
		const int32 Revision = ReplicatedPresentationState.Revision;
		// Remove only the old desire before Niagara can reenter. A callback that
		// starts another cue publishes a snapshot already missing this old loop.
		const bool bWasActiveLoop = ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(CueTag);
		ReplicatedPresentationState.ActiveLoopingCueTags.RemoveTag(CueTag);
		StopCueLocal(CueTag);
		if (!CanAcceptPresentation() || GetOwner() != PinnedOwner.Get() || GetWorld() != OriginalWorld.Get() ||
			!PinnedOwner->HasAuthority() || ActiveAttackInstance != Instance || ActiveAttackTag != Attack ||
			ReplicatedPresentationState.Revision != Revision) { return; }
		if (bWasActiveLoop)
		{
			PublishRecoveryState();
			MulticastPlayPresentationCue(Attack, CueTag, false, ++NextPresentationEventOrder, Instance, ReplicatedPresentationState);
		}
		return;
	}
	StopCueLocal(CueTag);
}

void UProject_JCombatPresentationComponent::StopCueLocal(const FGameplayTag CueTag)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatVFX_StopCueLocal);
	PendingLoopingCues.Remove(CueTag);
	if (PendingLoopingCues.IsEmpty()) { CancelReadyLoopApplications(); }
	if (TObjectPtr<UNiagaraComponent>* ActiveComponent = ActiveLoopingCues.Find(CueTag))
	{
		TStrongObjectPtr<UNiagaraComponent> Previous(ActiveComponent->Get());
		const bool bImmediate = ImmediateDestroyCueTags.HasTagExact(CueTag);
		ActiveLoopingCues.Remove(CueTag);
		ImmediateDestroyCueTags.RemoveTag(CueTag);
		ProjectJCombatPresentationDebug::StopComponent(Previous.Get(), bImmediate);
	}
}

void UProject_JCombatPresentationComponent::RefreshPresentation()
{
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	const uint32 Revision = ResetVisualAssetLeases();
	// Sets are resolved on demand, but an equipped visual/style change must never
	// leave a loop attached to a destroyed weapon actor.
	StopAllCues(true); // Optional profile/style arrival cannot erase an active producer-owned warning.
	if (CanAcceptPresentation() && VisualAssetRevision == Revision) { PreloadVisualAssets(); }
}

void UProject_JCombatPresentationComponent::GatherPresentationSets(FPresentationSets& Sets) const
{
	const auto* Character = Cast<AProject_JBaseCharacter>(GetOwner());
	const auto* Player = Cast<AProject_JPlayerCharacter>(Character);
	if (!Character) { return; }
	if (!Player && BasePresentationSet) { Sets.Add(BasePresentationSet); }
	if (const auto* Style = Player ? Player->GetCombatStyleDefinition() : nullptr)
	{
		if (Style->CombatPresentationSet) { Sets.Add(Style->CombatPresentationSet); }
	}
	if (const auto* Advancement = Character->GetAdvancementDefinition())
	{
		if (Advancement->CombatPresentationOverrideSet) { Sets.Add(Advancement->CombatPresentationOverrideSet); }
	}
	if (const auto* Weapon = Player ? Player->GetCurrentWeaponPresentationProfile() : nullptr)
	{
		if (Weapon->CosmeticPresentationOverrideSet) { Sets.Add(Weapon->CosmeticPresentationOverrideSet); }
	}
}

const FProject_JCombatVFXCueDefinition* UProject_JCombatPresentationComponent::ResolveCue(const FGameplayTag CueTag) const
{
	if (!ActiveAttackTag.IsValid()) { return nullptr; }
	FPresentationSets Sets;
	GatherPresentationSets(Sets);
	const FProject_JCombatVFXCueDefinition* Resolved = nullptr;
	for (const auto* Set : Sets)
	{
		if (const auto* Profile = Set->FindProfile(ActiveAttackTag))
		{
			if (const auto* Cue = Profile->FindCue(CueTag)) { Resolved = Cue; }
		}
	}
	return Resolved;
}

uint32 UProject_JCombatPresentationComponent::ResetVisualAssetLeases()
{
	const uint32 Revision = ++VisualAssetRevision;
	CancelReadyLoopApplications();
	const auto Previous = MoveTemp(VisualAssetLeases);
	PrefetchOnlyPaths.Reset();
	PendingLoopingCues.Reset();
	if (auto* Assets = GetWorld() ? GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>() : nullptr)
	{
		TStrongObjectPtr<UProject_JVisualAssetSubsystem> PinnedAssets(Assets);
		for (const auto& Pair : Previous) { Assets->Release(Pair.Value); }
	}
	return Revision;
}

bool UProject_JCombatPresentationComponent::EnsureVisualAsset(const FSoftObjectPath& Path, bool bAtCue)
{
	if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || !Path.IsValid()) { return false; }
	if (VisualAssetLeases.Contains(Path))
	{
		if (bAtCue) { PrefetchOnlyPaths.Remove(Path); }
		return true;
	}
	auto* Assets = GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>();
	if (!Assets) { return false; }
	const uint32 Revision = VisualAssetRevision;
	if (VisualAssetLeases.Num() >= MaxVisualAssetPaths)
	{
		if (!bAtCue) { return false; }
		// An actual cue can displace unused prewarming, then an idle earlier cue.
		// Already-playing/pending loops keep their residency and admission priority.
		const auto InUse = [this](const FSoftObjectPath& Candidate)
		{
			for (const auto& Pending : PendingLoopingCues)
			{
				if (Pending.Value.SystemPath == Candidate) { return true; }
			}
			for (const auto& Loop : ActiveLoopingCues)
			{
				if (IsValid(Loop.Value) && FSoftObjectPath(Loop.Value->GetAsset()) == Candidate) { return true; }
			}
			return false;
		};
		FSoftObjectPath Victim;
		for (const auto& Candidate : PrefetchOnlyPaths)
		{
			if (!InUse(Candidate)) { Victim = Candidate; break; }
		}
		if (!Victim.IsValid())
		{
			for (const auto& Candidate : VisualAssetLeases)
			{
				if (!InUse(Candidate.Key)) { Victim = Candidate.Key; break; }
			}
		}
		if (!Victim.IsValid()) { return false; }
		uint64 Token = 0;
		VisualAssetLeases.RemoveAndCopyValue(Victim, Token);
		PrefetchOnlyPaths.Remove(Victim);
		TStrongObjectPtr<ThisClass> PinnedThis(this);
		TStrongObjectPtr<UProject_JVisualAssetSubsystem> PinnedAssets(Assets);
		Assets->Release(Token);
		if (!CanAcceptPresentation() || VisualAssetRevision != Revision) { return false; }
	}
	TWeakObjectPtr<ThisClass> WeakThis(this);
	const uint64 Token = Assets->Request(this, Path, [WeakThis, Path, Revision](UObject* Asset)
	{
		if (auto* Self = WeakThis.Get()) { Self->OnVisualAssetReady(Path, Revision, Asset); }
	});
	// The shared subsystem delivers only from its bounded GT tick, never inline.
	if (Token)
	{
		VisualAssetLeases.Add(Path, Token);
		if (!bAtCue) { PrefetchOnlyPaths.Add(Path); }
	}
	return Token != 0;
}

void UProject_JCombatPresentationComponent::PreloadVisualAssets()
{
	if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatPresentation_Prefetch);
	FPresentationSets Sets;
	GatherPresentationSets(Sets);
	int32 Profiles = 0, Cues = 0;
	TMap<FGameplayTag, FGameplayTagContainer> SeenCuesByAttack;
	// Later overrides get first admission. Limits apply to cosmetic prewarming,
	// not gameplay catalog validity; an unvisited cue can request at use time.
	for (int32 Index = Sets.Num() - 1; Index >= 0; --Index)
	{
		TSet<FGameplayTag> SeenAttacksInSet;
		for (const auto& Entry : Sets[Index]->AttackPresentations)
		{
			if (++Profiles > MaxPrefetchProfiles) { return; }
			// FindProfile selects the first matching entry, including an empty one.
			if (SeenAttacksInSet.Contains(Entry.AttackTag)) { continue; }
			SeenAttacksInSet.Add(Entry.AttackTag);
			if (!Entry.Profile) { continue; }
			auto& SeenCues = SeenCuesByAttack.FindOrAdd(Entry.AttackTag);
			for (const auto& Cue : Entry.Profile->Cues)
			{
				if (++Cues > MaxPrefetchCueVisits || VisualAssetLeases.Num() >= MaxVisualAssetPaths) { return; }
				if (!Cue.CueTag.IsValid() || SeenCues.HasTagExact(Cue.CueTag)) { continue; }
				// A winning empty cue intentionally suppresses a lower set's payload.
				SeenCues.AddTag(Cue.CueTag);
				if (!Cue.NiagaraSystem.IsNull()) { EnsureVisualAsset(Cue.NiagaraSystem.ToSoftObjectPath()); }
			}
		}
	}
}

void UProject_JCombatPresentationComponent::OnVisualAssetReady(const FSoftObjectPath& Path, uint32 Revision, UObject* Asset)
{
	check(IsInGameThread());
	if (!CanAcceptPresentation() || VisualAssetRevision != Revision || !VisualAssetLeases.Contains(Path)) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TStrongObjectPtr<UNiagaraSystem> PinnedSystem(Cast<UNiagaraSystem>(Asset));
	if (!PinnedSystem.IsValid())
	{
		uint64 Token = 0;
		VisualAssetLeases.RemoveAndCopyValue(Path, Token);
		PrefetchOnlyPaths.Remove(Path);
		for (auto It = PendingLoopingCues.CreateIterator(); It; ++It)
		{
			if (It.Value().SystemPath == Path) { It.RemoveCurrent(); }
		}
		if (auto* Assets = GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>()) { Assets->Release(Token); }
		return;
	}
	bool bDeferredAny = false;
	const uint32 Instance = ActiveAttackInstance;
	TArray<FGameplayTag> PendingTags;
	PendingLoopingCues.GetKeys(PendingTags);
	for (const auto& Tag : PendingTags)
	{
		if (!CanAcceptPresentation() || VisualAssetRevision != Revision) { return; }
		const auto* Pending = PendingLoopingCues.Find(Tag);
		if (!Pending || Pending->SystemPath != Path || Pending->AttackInstance != ActiveAttackInstance) { continue; }
		const auto* Cue = ResolveCue(Tag);
		if (!Cue || !Cue->bLooping || Cue->NiagaraSystem.ToSoftObjectPath() != Path)
		{
			PendingLoopingCues.Remove(Tag); continue;
		}
		if (ShouldBudgetRemoteSpawn(*Cue))
		{
			// Asset delivery is one loader application, not permission to drain
			// every same-system cue inside that single callback.
			PendingLoopingCues.FindChecked(Tag).bReadyForBudget = true; bDeferredAny = true;
		}
		else { PlayCueLocal(Tag); }
	}
	if (bDeferredAny && CanAcceptPresentation() && VisualAssetRevision == Revision && ActiveAttackInstance == Instance)
	{ QueueReadyLoopApplications(); }
}

void UProject_JCombatPresentationComponent::CancelReadyLoopApplications()
{
	if (auto* Budget = GetWorld() ? GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>() : nullptr)
	{ Budget->Cancel(this); }
}

void UProject_JCombatPresentationComponent::QueueReadyLoopApplications()
{
	if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) { return; }
	const uint32 Revision = VisualAssetRevision, Instance = ActiveAttackInstance;
	bool bHasReady = false;
	for (const auto& Pending : PendingLoopingCues)
	{
		if (Pending.Value.bReadyForBudget && Pending.Value.AttackInstance == Instance) { bHasReady = true; break; }
	}
	if (!bHasReady) { return; }
	auto* Budget = GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
	if (Budget && Budget->Request(this, Revision)) { return; }
	if (!CanAcceptPresentation() || VisualAssetRevision != Revision || ActiveAttackInstance != Instance) { return; }
	// Match existing cosmetic load/spawn failure: terminal for only the ready
	// desires of this incarnation. No synchronous drain, retry timer, or stranded
	// unowned continuation; another path still loading remains untouched.
	for (auto It = PendingLoopingCues.CreateIterator(); It; ++It)
	{
		if (It.Value().bReadyForBudget && It.Value().AttackInstance == Instance)
		{
			// A same-attack recovery snapshot must not silently replay a rejected
			// cosmetic as a new warm cue. StopAll/new attack owns this consumed mark.
			StartedCueTags.AddTag(It.Key());
			++RejectedReadyLoops; It.RemoveCurrent();
		}
	}
}

void UProject_JCombatPresentationComponent::ApplyBudgetedReadyLoop(uint64 Revision)
{
	check(IsInGameThread());
	if (Revision != VisualAssetRevision || !CanAcceptPresentation()
		|| GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	const uint32 Instance = ActiveAttackInstance;
	FGameplayTag NextCue;
	FPendingLoop Desired;
	for (const auto& Pending : PendingLoopingCues)
	{
		if (Pending.Value.bReadyForBudget && Pending.Value.AttackInstance == Instance)
		{ NextCue = Pending.Key; Desired = Pending.Value; break; }
	}
	if (!NextCue.IsValid()) { return; }
	const auto* Cue = ResolveCue(NextCue);
	if (!Cue || !Cue->bLooping || Cue->NiagaraSystem.ToSoftObjectPath() != Desired.SystemPath
		|| ActiveLoopingCues.Contains(NextCue) || StartedCueTags.HasTagExact(NextCue))
	{ PendingLoopingCues.Remove(NextCue); }
	else
	{
		// Only this validated descriptor receives the admitted apply. No callback
		// or wait occurs between consumption and the existing spawn path; all other
		// ready tags still reject immediate recovery/reentrant playback.
		PendingLoopingCues.FindChecked(NextCue).bReadyForBudget = false;
		PlayCueLocal(NextCue, true); // Only this exact validated descriptor owns the already charged apply.
	}
	if (CanAcceptPresentation() && VisualAssetRevision == Revision && ActiveAttackInstance == Instance)
	{ QueueReadyLoopApplications(); } // Tail re-entry shares the same Tick's apply/visit/time limits.
}

void UProject_JCombatPresentationComponent::PublishRecoveryState()
{
	if (!CanAcceptPresentation() || !GetOwner()->HasAuthority())
	{
		return;
	}

	++ReplicatedPresentationState.Revision;
	ReplicatedPresentationState.EventOrder = ++NextPresentationEventOrder;
	GetOwner()->ForceNetUpdate();
}

void UProject_JCombatPresentationComponent::ApplyReplicatedState()
{
	if (!CanAcceptPresentation()) { return; }
	if (ReplicatedPresentationState.Revision <= LastAppliedPresentationRevision
		|| ReplicatedPresentationState.EventOrder <= LastAppliedRecoveryEventOrder)
	{
		return;
	}

	LastAppliedPresentationRevision = ReplicatedPresentationState.Revision;
	LastAppliedRecoveryEventOrder = ReplicatedPresentationState.EventOrder;
	LastAppliedPresentationEventOrder = FMath::Max(LastAppliedPresentationEventOrder, ReplicatedPresentationState.EventOrder);
	if (ActiveAttackInstance != ReplicatedPresentationState.AttackInstance || ActiveAttackTag != ReplicatedPresentationState.ActiveAttackTag)
	{
		StopAllCues();
	}
	else
	{
		TArray<FGameplayTag> Existing;
		ActiveLoopingCues.GetKeys(Existing);
		for (const auto& Pending : PendingLoopingCues) { Existing.AddUnique(Pending.Key); }
		for (const auto& Tag : Existing)
		{
			if (!ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag)) { StopCueLocal(Tag); }
		}
	}
	ActiveAttackInstance = ReplicatedPresentationState.AttackInstance;
	ActiveAttackTag = ReplicatedPresentationState.ActiveAttackTag;
    const uint32 WarningInstance = ActiveAttackInstance;
    const int32 WarningRevision = ReplicatedPresentationState.Revision;
    ActiveWarningCues = ReplicatedPresentationState.ActiveWarningCueTags;
    UpdateWarningMarker();
    if (!CanAcceptPresentation() || ActiveAttackInstance != WarningInstance ||
        ReplicatedPresentationState.Revision != WarningRevision) { return; }
	if (AActor* Owner = GetOwner())
	{
		if (UProject_JWeaponPresentationComponent* Weapon = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Weapon->SetActiveAttackPresentation(ActiveAttackTag);
		}
	}
	for (const FGameplayTag& CueTag : ReplicatedPresentationState.ActiveLoopingCueTags)
	{
		PlayCueLocal(CueTag);
	}
}

void UProject_JCombatPresentationComponent::OnRep_PresentationState(FProject_JReplicatedCombatPresentationState PreviousState)
{
	ApplyReplicatedState();
}

void UProject_JCombatPresentationComponent::MulticastPlayPresentationCue_Implementation(
	const FGameplayTag AttackTag, const FGameplayTag CueTag, const bool bStart, const int32 EventOrder, const uint32 AttackInstance,
	const FProject_JReplicatedCombatPresentationState& RecoveryState)
{
	if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || (GetOwner() && GetOwner()->HasAuthority()) ||
		EventOrder <= LastAppliedPresentationEventOrder)
	{
		return;
	}

    // A cue delta cannot certify that every other persistent cue was seen.
    // Carry the existing complete recovery snapshot with the event, so advancing
    // its ordering boundary also recovers lost starts/stops of other cues.
    if (RecoveryState.Revision > LastAppliedPresentationRevision &&
        RecoveryState.EventOrder > LastAppliedRecoveryEventOrder)
    {
        ReplicatedPresentationState = RecoveryState;
        ApplyReplicatedState();
    }
    if (!CanAcceptPresentation()) { return; }
	LastAppliedPresentationEventOrder = FMath::Max(LastAppliedPresentationEventOrder, EventOrder);
	if (bStart)
	{
		if (ActiveAttackTag != AttackTag || ActiveAttackInstance != AttackInstance)
		{
			StopAllCues(); LastAppliedRecoveryEventOrder = EventOrder;
		}
		ActiveAttackTag = AttackTag;
		ActiveAttackInstance = AttackInstance;
		if (AActor* Owner = GetOwner())
		{
			if (UProject_JWeaponPresentationComponent* Weapon = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
			{
				Weapon->SetActiveAttackPresentation(AttackTag);
			}
		}
		if (const auto* Cue = ResolveCue(CueTag); Cue && Cue->bLooping) { LastAppliedRecoveryEventOrder = EventOrder; }
		PlayCueLocal(CueTag);
	}
	else
	{
		LastAppliedRecoveryEventOrder = EventOrder;
        EndWarningCue(CueTag);
		StopCueLocal(CueTag);
	}
}

void UProject_JCombatPresentationComponent::MulticastWarningPresentationState_Implementation(
    const FProject_JReplicatedCombatPresentationState& RecoveryState)
{
    const bool bEligible = CanAcceptPresentation() && GetNetMode() != NM_DedicatedServer && !GetOwner()->HasAuthority()
        && RecoveryState.Revision > LastAppliedPresentationRevision
        && RecoveryState.EventOrder > LastAppliedRecoveryEventOrder;
    // Opt-in transport evidence includes superseded snapshots. Entry/eligibility
    // is distinct from committing a current warning or rendering it this frame.
    UE_LOG(LogProjectJCombatPresentation, Verbose,
        TEXT("WARNING_RPC owner=%s mode=%d role=%d frame=%llu instance=%u revision=%d order=%d warnings=%d previous_revision=%d previous_order=%d eligible=%d"),
        *GetNameSafe(GetOwner()), GetWorld() ? int32(GetNetMode()) : -1, GetOwner() ? int32(GetOwner()->GetLocalRole()) : 0,
        static_cast<unsigned long long>(GFrameCounter), RecoveryState.AttackInstance, RecoveryState.Revision, RecoveryState.EventOrder,
        RecoveryState.ActiveWarningCueTags.Num(), LastAppliedPresentationRevision, LastAppliedRecoveryEventOrder, bEligible);
    if (!bEligible) { return; }
    // Same monotonic consumer as property recovery: a delayed reliable start cannot resurrect a newer end/attack.
    // Delivery is not a promise to arrive or render before authoritative damage.
    ReplicatedPresentationState = RecoveryState;
    ApplyReplicatedState();
}

void UProject_JCombatPresentationComponent::MulticastEndAttackPresentation_Implementation(const int32 EventOrder)
{
	if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || (GetOwner() && GetOwner()->HasAuthority()) ||
		EventOrder <= LastAppliedPresentationEventOrder)
	{
		return;
	}

	LastAppliedPresentationEventOrder = EventOrder;
	LastAppliedRecoveryEventOrder = EventOrder;
	EndAttackPresentation();
}

void UProject_JCombatPresentationComponent::StopAllCues(bool bKeepWarnings)
{
	CancelReadyLoopApplications();
    TStrongObjectPtr<ULineBatchComponent> RetiredMarker;
    if (!bKeepWarnings)
    {
        CueLeases.Reset(); WarningCueLeases.Reset(); ActiveWarningCues.Reset();
        RetiredMarker.Reset(WarningMarker.Get()); WarningMarker = nullptr;
    }
    else
    {
        for (auto It = CueLeases.CreateIterator(); It; ++It)
        {
            if (!WarningCueLeases.Contains(It.Key())) { It.RemoveCurrent(); }
        }
    }
	PendingLoopingCues.Reset();
	const auto Previous = MoveTemp(ActiveLoopingCues);
	const auto PreviousImmediate = MoveTemp(ImmediateDestroyCueTags);
	StartedCueTags.Reset();
	// Niagara stop callbacks may reenter or collect garbage. Detach the whole
	// previous generation and pin it before calling into the engine.
	TArray<TStrongObjectPtr<UNiagaraComponent>> Pins;
	Pins.Reserve(Previous.Num());
	for (const auto& Pair : Previous) { Pins.Emplace(Pair.Value.Get()); }
    if (RetiredMarker.IsValid()) { RetiredMarker->DestroyComponent(); }
	int32 Index = 0;
	for (const auto& Pair : Previous)
	{
		ProjectJCombatPresentationDebug::StopComponent(Pins[Index++].Get(), PreviousImmediate.HasTagExact(Pair.Key));
	}
}

uint64 UProject_JCombatPresentationComponent::BeginCueLease(FGameplayTag CueTag, bool bGameplayWarning)
{
	if (!CanAcceptPresentation()) { return 0; }
	const auto* Cue = ResolveCue(CueTag);
	if (!ActiveAttackTag.IsValid() || !CueTag.IsValid() || ((!Cue || Cue->NiagaraSystem.IsNull()) && !bGameplayWarning)) { return 0; }
	const uint64 Token = ++NextCueLeaseToken;
	CueLeases.Add(Token, CueTag);
    if (bGameplayWarning) { WarningCueLeases.Add(Token); ActiveWarningCues.AddTag(CueTag); }
	PlayCue(CueTag);
	return Token;
}

TWeakObjectPtr<UNiagaraComponent> UProject_JCombatPresentationComponent::GetCueLeaseComponent(uint64 Token) const
{
	check(IsInGameThread());
	if (!Token || !CanAcceptPresentation()) { return nullptr; }
	const FGameplayTag* CueTag = CueLeases.Find(Token);
	const TObjectPtr<UNiagaraComponent>* Loop = CueTag ? ActiveLoopingCues.Find(*CueTag) : nullptr;
	UNiagaraComponent* Component = Loop ? Loop->Get() : nullptr;
	if (!IsValid(Component) || Component->IsBeingDestroyed() || Component->GetWorld() != GetWorld()) { return nullptr; }
	return Component;
}

void UProject_JCombatPresentationComponent::EndCueLease(uint64 Token)
{
    const TStrongObjectPtr<ThisClass> Self(this);
    const uint32 Instance = ActiveAttackInstance;
	FGameplayTag CueTag;
	if (!Token || !CueLeases.RemoveAndCopyValue(Token, CueTag)) { return; }
    if (WarningCueLeases.Remove(Token))
    {
        bool bHasWarning = false;
        for (uint64 WarningToken : WarningCueLeases)
        {
            const FGameplayTag* Tag = CueLeases.Find(WarningToken);
            if (Tag && *Tag == CueTag) { bHasWarning = true; break; }
        }
        if (!bHasWarning) { EndWarningCue(CueTag); }
        if (!CanAcceptPresentation() || ActiveAttackInstance != Instance) { return; }
    }
	for (const auto& Lease : CueLeases) { if (Lease.Value == CueTag) { return; } }
	StopCue(CueTag);
}

void UProject_JCombatPresentationComponent::EndWarningCue(FGameplayTag CueTag)
{
    if (!ActiveWarningCues.HasTagExact(CueTag)) { return; }
    // A remote snapshot's live warning is not owned by a possibly missing/local cosmetic notify.
    if (GetOwner() && !GetOwner()->HasAuthority() && ReplicatedPresentationState.AttackInstance == ActiveAttackInstance
        && ReplicatedPresentationState.ActiveWarningCueTags.HasTagExact(CueTag)) { return; }
    ActiveWarningCues.RemoveTag(CueTag);
    if (GetOwner() && GetOwner()->HasAuthority() && ReplicatedPresentationState.ActiveWarningCueTags.HasTagExact(CueTag))
    {
        ReplicatedPresentationState.ActiveWarningCueTags.RemoveTag(CueTag);
        PublishRecoveryState();
        if (CanAcceptPresentation()) { MulticastWarningPresentationState(ReplicatedPresentationState); }
    }
    UpdateWarningMarker();
}

void UProject_JCombatPresentationComponent::UpdateWarningMarker()
{
    check(IsInGameThread());
    if (ActiveWarningCues.IsEmpty())
    {
        const TStrongObjectPtr<ULineBatchComponent> Retired(WarningMarker.Get());
        WarningMarker = nullptr;
        if (Retired.IsValid()) { Retired->DestroyComponent(); }
        return;
    }
    if (!CanAcceptPresentation() || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) { return; }
    if (IsValid(WarningMarker) && !WarningMarker->IsBeingDestroyed() && WarningMarker->IsRegistered()
        && WarningMarker->GetAttachParent() == GetOwner()->GetRootComponent()) { return; }
    const TStrongObjectPtr<ThisClass> Self(this);
    const TStrongObjectPtr<AActor> Owner(GetOwner());
    const TStrongObjectPtr<UWorld> World(GetWorld());
    const uint32 Instance = ActiveAttackInstance;
    const TStrongObjectPtr<USceneComponent> Root(Owner->GetRootComponent());
    if (!Root.IsValid() || Root->IsBeingDestroyed() || !Root->IsRegistered() || Root->GetWorld() != World.Get()) { return; }
    const TStrongObjectPtr<ULineBatchComponent> Previous(WarningMarker.Get());
    WarningMarker = nullptr;
    if (Previous.IsValid() && !Previous->IsBeingDestroyed()) { Previous->DestroyComponent(); }
    if (!CanAcceptPresentation() || ActiveAttackInstance != Instance || ActiveWarningCues.IsEmpty()) { return; }
    const TStrongObjectPtr<UProject_JGameplayWarningMarkerComponent> Marker(
        NewObject<UProject_JGameplayWarningMarkerComponent>(Owner.Get(), NAME_None, RF_Transient));
    WarningMarker = Marker.Get(); // Publish ownership before native registration can reenter.
    Marker->SetupAttachment(Root.Get());
    Owner->AddInstanceComponent(Marker.Get());
    Marker->RegisterComponent();
    Marker->SetComponentTickEnabled(false);
    if (!CanAcceptPresentation() || GetOwner() != Owner.Get() || GetWorld() != World.Get() ||
        ActiveAttackInstance != Instance || ActiveWarningCues.IsEmpty() || WarningMarker != Marker.Get()
        || Owner->GetRootComponent() != Root.Get() || Marker->GetAttachParent() != Root.Get())
    {
        if (WarningMarker == Marker.Get()) { WarningMarker = nullptr; }
        if (!Marker->IsBeingDestroyed()) { Marker->DestroyComponent(); }
    }
}
