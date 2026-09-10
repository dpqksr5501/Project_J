#include "Components/Project_JCombatPresentationComponent.h"

#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Project_JBaseCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatPresentation, Log, All);

namespace ProjectJCombatPresentationDebug
{
	static TAutoConsoleVariable<int32> CVarPool(
		TEXT("ProjectJ.Combat.Presentation.Pool"), 1,
		TEXT("Use engine Niagara pools for cosmetic cues. 0 restores unpooled creation for comparison."));

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
	EndAttackPresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatPresentationComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	StopAllCues();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JCombatPresentationComponent::BeginAttackPresentation(const FGameplayTag AttackTag)
{
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
		StopAllCues();
		ActiveAttackTag = AttackTag;
		++ActiveAttackInstance;
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
			PublishRecoveryState();
		}
	}
}

void UProject_JCombatPresentationComponent::EndAttackPresentation()
{
	const bool bHadAttack = ActiveAttackTag.IsValid() || !ActiveLoopingCues.IsEmpty()
		|| ReplicatedPresentationState.ActiveAttackTag.IsValid();
	if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] EndAttack Owner=%s PreviousAttack=%s Authority=%d"),
			*GetNameSafe(GetOwner()), *ActiveAttackTag.ToString(), GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
	}
	StopAllCues();
	ActiveAttackTag = FGameplayTag();
	if (bHadAttack && GetOwner() && GetOwner()->HasAuthority())
	{
		ReplicatedPresentationState.ActiveAttackTag = FGameplayTag();
		ReplicatedPresentationState.ActiveLoopingCueTags.Reset();
		PublishRecoveryState();
		MulticastEndAttackPresentation(++NextPresentationEventOrder);
	}
}

void UProject_JCombatPresentationComponent::PlayCue(const FGameplayTag CueTag)
{
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
	if (!Cue || !Cue->NiagaraSystem)
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
			*GetNameSafe(GetOwner()), *ActiveAttackTag.ToString(), *CueTag.ToString(), *GetNameSafe(Cue->NiagaraSystem),
			GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (Cue->bLooping)
		{
			if (!ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(CueTag))
			{
				ReplicatedPresentationState.ActiveLoopingCueTags.AddTag(CueTag);
				PublishRecoveryState();
			}
		}

		// A listen server is both the authority and a local viewer.  The multicast
		// handler deliberately skips authority to avoid duplicate effects, so play
		// the cosmetic locally before notifying remote clients.
		PlayCueLocal(CueTag);
		MulticastPlayPresentationCue(ActiveAttackTag, CueTag, true, ++NextPresentationEventOrder, ActiveAttackInstance);
		return;
	}

	PlayCueLocal(CueTag);
}

void UProject_JCombatPresentationComponent::PlayCueLocal(const FGameplayTag CueTag)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatPresentation_Spawn);
	if (GetNetMode() == NM_DedicatedServer)
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] Local spawn skipped: dedicated server. Cue=%s"), *CueTag.ToString());
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
	if (StartedCueTags.HasTagExact(CueTag))
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Log, TEXT("[CombatVFX] Local spawn skipped: cue already started for this attack. Cue=%s"), *CueTag.ToString());
		}
		return;
	}

	const FProject_JCombatVFXCueDefinition* Cue = ResolveCue(CueTag);
	if (!Cue || !Cue->NiagaraSystem)
	{
		if (ProjectJCombatPresentationDebug::IsEnabled())
		{
			UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: cue could not be resolved. Attack=%s Cue=%s"),
				*ActiveAttackTag.ToString(), *CueTag.ToString());
		}
		return;
	}

	USceneComponent* AttachComponent = nullptr;
	if (AProject_JBaseCharacter* Character = Cast<AProject_JBaseCharacter>(GetOwner()))
	{
		if (Cue->AttachmentTarget == EProject_JCombatVFXAttachmentTarget::Weapon)
		{
			if (UProject_JWeaponPresentationComponent* WeaponPresentation = Character->FindComponentByClass<UProject_JWeaponPresentationComponent>())
			{
				AttachComponent = WeaponPresentation->GetWeaponVFXAttachmentComponent(Cue->AttachSocketName);
			}
		}
		else if (Cue->AttachmentTarget == EProject_JCombatVFXAttachmentTarget::CharacterMesh)
		{
			AttachComponent = Character->GetMesh();
		}
	}
	else if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: owner is not AProject_JBaseCharacter. Owner=%s"), *GetNameSafe(GetOwner()));
	}

	UNiagaraComponent* NiagaraComponent = nullptr;
	const ENCPoolMethod Pool = ProjectJCombatPresentationDebug::CVarPool.GetValueOnGameThread()
		? (Cue->bLooping ? ENCPoolMethod::ManualRelease : ENCPoolMethod::AutoRelease) : ENCPoolMethod::None;
	if (Cue->AttachmentTarget == EProject_JCombatVFXAttachmentTarget::World)
	{
		if (UWorld* World = GetWorld())
		{
			const FTransform OwnerTransform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
			const FTransform SpawnTransform = Cue->RelativeTransform * OwnerTransform;
			NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World, Cue->NiagaraSystem, SpawnTransform.GetLocation(), SpawnTransform.Rotator(), SpawnTransform.GetScale3D(), true, true, Pool);
		}
	}
	else if (AttachComponent)
	{
		NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			Cue->NiagaraSystem, AttachComponent, Cue->AttachSocketName,
			Cue->RelativeTransform.GetLocation(), Cue->RelativeTransform.Rotator(),
			EAttachLocation::KeepRelativeOffset, true, true, Pool);
		if (NiagaraComponent)
		{
			NiagaraComponent->SetRelativeScale3D(Cue->RelativeTransform.GetScale3D());
		}
	}
	else if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		UE_LOG(LogProjectJCombatPresentation, Warning, TEXT("[CombatVFX] Local spawn failed: no attachment component. Target=%d Socket=%s Owner=%s"),
			static_cast<int32>(Cue->AttachmentTarget), *Cue->AttachSocketName.ToString(), *GetNameSafe(GetOwner()));
	}

	if (ProjectJCombatPresentationDebug::IsEnabled())
	{
		if (NiagaraComponent)
		{
			UE_LOG(LogProjectJCombatPresentation, Log,
				TEXT("[CombatVFX] Niagara spawned. System=%s Target=%d Socket=%s AttachComponent=%s"),
				*GetNameSafe(Cue->NiagaraSystem), static_cast<int32>(Cue->AttachmentTarget),
				*Cue->AttachSocketName.ToString(), *GetNameSafe(AttachComponent));
		}
		else
		{
			UE_LOG(LogProjectJCombatPresentation, Warning,
				TEXT("[CombatVFX] Niagara spawn returned null. System=%s Target=%d Socket=%s AttachComponent=%s"),
				*GetNameSafe(Cue->NiagaraSystem), static_cast<int32>(Cue->AttachmentTarget),
				*Cue->AttachSocketName.ToString(), *GetNameSafe(AttachComponent));
		}
	}

	if (Cue->bLooping && NiagaraComponent)
	{
		ActiveLoopingCues.Add(CueTag, NiagaraComponent);
		if (Cue->bDestroyImmediatelyOnStop)
		{
			ImmediateDestroyCueTags.AddTag(CueTag);
		}
	}
	if (NiagaraComponent)
	{
		StartedCueTags.AddTag(CueTag);
	}
}

void UProject_JCombatPresentationComponent::StopCue(const FGameplayTag CueTag)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const bool bWasActiveLoop = ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(CueTag);
		StopCueLocal(CueTag);
		ReplicatedPresentationState.ActiveLoopingCueTags.RemoveTag(CueTag);
		if (bWasActiveLoop)
		{
			PublishRecoveryState();
			MulticastPlayPresentationCue(ActiveAttackTag, CueTag, false, ++NextPresentationEventOrder, ActiveAttackInstance);
		}
		return;
	}

	StopCueLocal(CueTag);
}

void UProject_JCombatPresentationComponent::StopCueLocal(const FGameplayTag CueTag)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatVFX_StopCueLocal);
	if (TObjectPtr<UNiagaraComponent>* ActiveComponent = ActiveLoopingCues.Find(CueTag))
	{
		ProjectJCombatPresentationDebug::StopComponent(*ActiveComponent, ImmediateDestroyCueTags.HasTagExact(CueTag));
		ActiveLoopingCues.Remove(CueTag);
		ImmediateDestroyCueTags.RemoveTag(CueTag);
	}
}

void UProject_JCombatPresentationComponent::RefreshPresentation()
{
	// Sets are resolved on demand, but an equipped visual/style change must never
	// leave a loop attached to a destroyed weapon actor.
	StopAllCues();
}

const FProject_JCombatVFXCueDefinition* UProject_JCombatPresentationComponent::ResolveCue(const FGameplayTag CueTag) const
{
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetOwner());
	const AProject_JBaseCharacter* Character = Cast<AProject_JBaseCharacter>(GetOwner());
	if (!Character || !ActiveAttackTag.IsValid())
	{
		return nullptr;
	}

	const FProject_JCombatVFXCueDefinition* ResolvedCue = nullptr;
	const auto ApplySet = [this, CueTag, &ResolvedCue](const UProject_JCombatPresentationSet* Set)
	{
		if (const UProject_JAttackPresentationProfile* Profile = Set ? Set->FindProfile(ActiveAttackTag) : nullptr)
		{
			if (const FProject_JCombatVFXCueDefinition* OverrideCue = Profile->FindCue(CueTag))
			{
				ResolvedCue = OverrideCue;
			}
		}
	};

	if (!PlayerCharacter) { ApplySet(BasePresentationSet); }
	if (const UProject_JCombatStyleDefinition* Style = PlayerCharacter ? PlayerCharacter->GetCombatStyleDefinition() : nullptr)
	{
		ApplySet(Style->CombatPresentationSet);
	}
	if (const UProject_JCharacterAdvancementDefinition* Advancement = Character->GetAdvancementDefinition())
	{
		ApplySet(Advancement->CombatPresentationOverrideSet);
	}
	if (const UProject_JWeaponPresentationProfile* WeaponProfile = PlayerCharacter ? PlayerCharacter->GetCurrentWeaponPresentationProfile() : nullptr)
	{
		ApplySet(WeaponProfile->CosmeticPresentationOverrideSet);
	}
	return ResolvedCue;
}

void UProject_JCombatPresentationComponent::PublishRecoveryState()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	++ReplicatedPresentationState.Revision;
	ReplicatedPresentationState.EventOrder = ++NextPresentationEventOrder;
	GetOwner()->ForceNetUpdate();
}

void UProject_JCombatPresentationComponent::ApplyReplicatedState()
{
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
		for (const auto& Tag : Existing)
		{
			if (!ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag)) { StopCueLocal(Tag); }
		}
	}
	ActiveAttackInstance = ReplicatedPresentationState.AttackInstance;
	ActiveAttackTag = ReplicatedPresentationState.ActiveAttackTag;
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
	const FGameplayTag AttackTag, const FGameplayTag CueTag, const bool bStart, const int32 EventOrder, const uint32 AttackInstance)
{
	if (GetNetMode() == NM_DedicatedServer || (GetOwner() && GetOwner()->HasAuthority()) ||
		EventOrder <= LastAppliedPresentationEventOrder)
	{
		return;
	}

	LastAppliedPresentationEventOrder = EventOrder;
	if (bStart)
	{
		if (ActiveAttackTag != AttackTag || ActiveAttackInstance != AttackInstance)
		{
			StopAllCues(); LastAppliedRecoveryEventOrder = EventOrder;
		}
		ActiveAttackTag = AttackTag;
		ActiveAttackInstance = AttackInstance;
		if (const auto* Cue = ResolveCue(CueTag); Cue && Cue->bLooping) { LastAppliedRecoveryEventOrder = EventOrder; }
		PlayCueLocal(CueTag);
	}
	else
	{
		LastAppliedRecoveryEventOrder = EventOrder;
		StopCueLocal(CueTag);
	}
}

void UProject_JCombatPresentationComponent::MulticastEndAttackPresentation_Implementation(const int32 EventOrder)
{
	if (GetNetMode() == NM_DedicatedServer || (GetOwner() && GetOwner()->HasAuthority()) ||
		EventOrder <= LastAppliedPresentationEventOrder)
	{
		return;
	}

	LastAppliedPresentationEventOrder = EventOrder;
	LastAppliedRecoveryEventOrder = EventOrder;
	EndAttackPresentation();
}

void UProject_JCombatPresentationComponent::StopAllCues()
{
	for (TPair<FGameplayTag, TObjectPtr<UNiagaraComponent>>& Pair : ActiveLoopingCues)
	{
		ProjectJCombatPresentationDebug::StopComponent(Pair.Value, ImmediateDestroyCueTags.HasTagExact(Pair.Key));
	}
	ActiveLoopingCues.Reset();
	ImmediateDestroyCueTags.Reset();
	StartedCueTags.Reset();
}
