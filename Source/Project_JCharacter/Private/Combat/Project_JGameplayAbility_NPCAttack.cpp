#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Project_JGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPCAttack, Log, All);
namespace
{
	TAutoConsoleVariable<int32> CVarNPCAttackDebug(TEXT("ProjectJ.NPC.Attack.Debug"), 0,
		TEXT("Logs NPC attack activation/rejection/end. 0: off, 1: on."), ECVF_Default);
}

UProject_JGameplayAbility_NPCAttack::UProject_JGameplayAbility_NPCAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationOwnedTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
	ActivationBlockedTags.AddTag(FProject_JGameplayTags::Get().State_Attacking);
}
UProject_JAttackDefinition* UProject_JGameplayAbility_NPCAttack::ResolveAttack(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info) const
{
	const auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (auto* Source = Spec ? Cast<UProject_JAttackDefinition>(Spec->SourceObject.Get()) : nullptr) { return Source; }
	return AttackDefinition;
}
bool UProject_JGameplayAbility_NPCAttack::ConfigureHitEvent(FGameplayTag EventTag)
{
	if (HasAnyFlags(RF_ClassDefaultObject) || IsActive() || !EventTag.IsValid()
		|| !GetAvatarActorFromActorInfo() || !GetAvatarActorFromActorInfo()->HasAuthority()) { return false; }
	HitEventTag = EventTag; return true;
}
bool UProject_JGameplayAbility_NPCAttack::SupportsMovementPolicy(const UProject_JAttackDefinition& Definition)
{
	return !Definition.bUseFlyingMovementModeForRootMotion
		&& (Definition.MovementPolicy == EProject_JAttackMovementPolicy::RootMotionMontage
			|| (Definition.MovementPolicy == EProject_JAttackMovementPolicy::InPlace
				&& Definition.Montage && !Definition.Montage->HasRootMotion()));
}

FName UProject_JGameplayAbility_NPCAttack::ValidateAttackContext(const AProject_JNPCCharacter* NPC,
	const UProject_JAttackDefinition* Attack) const
{
	if (!IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed()) { return TEXT("InvalidAuthorityOrAvatar"); }
	const auto* Action = NPC ? NPC->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr;
	if (!Action || !Action->CanCommitToTarget(Action->GetIntentTarget())) { return TEXT("InvalidTargetRangeLOSOrContext"); }
	const auto* Anim = NPC->GetMesh() ? NPC->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim) { return TEXT("MissingAnimInstance"); }
	if (!NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>()) { return TEXT("MissingHitValidationComponent"); }
	if (!Attack || !Attack->AttackTag.IsValid() || !Attack->Montage || !Attack->DamageEffect) { return TEXT("IncompleteAttackDefinition"); }
	if (!HitEventTag.IsValid()) { return TEXT("MissingHitEventTag"); }
	if (!FMath::IsFinite(Attack->PlayRate) || Attack->PlayRate <= 0 || Attack->Montage->GetPlayLength() <= 0) { return TEXT("InvalidMontageRateOrLength"); }
	if (!Attack->MontageSectionName.IsNone() && Attack->Montage->GetSectionIndex(Attack->MontageSectionName) == INDEX_NONE) { return TEXT("MissingMontageSection"); }
	if (!SupportsMovementPolicy(*Attack)) { return TEXT("UnsupportedWarpedAerialOrInPlaceRootMotion"); }
	if (!NPC->GetCharacterMovement() || !NPC->GetCharacterMovement()->IsMovingOnGround()) { return TEXT("NotGrounded"); }
	// Keep the authored ABP policy consistent on server and simulated proxies; do not change it only on authority.
	if (Attack->Montage->HasRootMotion() && Anim->RootMotionMode != ERootMotionMode::RootMotionFromMontagesOnly)
	{
		return TEXT("ABPRequiresRootMotionFromMontagesOnly");
	}
	return NAME_None;
}

void UProject_JGameplayAbility_NPCAttack::LogAttackRejection(FName Reason) const
{
	UE_CLOG(CVarNPCAttackDebug.GetValueOnGameThread() != 0, LogProjectJNPCAttack, Log,
		TEXT("Rejected Owner=%s Reason=%s"), *GetNameSafe(GetAvatarActorFromActorInfo()), *Reason.ToString());
}

bool UProject_JGameplayAbility_NPCAttack::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* RelevantTags) const
{
	if (bEndingAttack || !Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, RelevantTags)) { LogAttackRejection(TEXT("GASGate")); return false; }
	const auto* NPC = Info ? Cast<AProject_JNPCCharacter>(Info->AvatarActor.Get()) : nullptr;
	const auto* Attack = ResolveAttack(Handle, Info);
	const FName Failure = ValidateAttackContext(NPC, Attack);
	if (!Failure.IsNone()) { LogAttackRejection(Failure); return false; }
	FGameplayTagContainer Tags; Info->AbilitySystemComponent->GetOwnedGameplayTags(Tags);
	const bool bTagsAllow = Tags.HasAll(Attack->RequiredOwnerTags) && !Tags.HasAny(Attack->BlockedOwnerTags);
	if (!bTagsAllow) { LogAttackRejection(TEXT("AttackOwnerTags")); }
	return bTagsAllow;
}
void UProject_JGameplayAbility_NPCAttack::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	auto* NPC = Info ? Cast<AProject_JNPCCharacter>(Info->AvatarActor.Get()) : nullptr;
	auto* Action = NPC ? NPC->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr;
	const TWeakObjectPtr<AActor> RequestedTarget = Action ? Action->GetIntentTarget() : nullptr;
	const TWeakObjectPtr<UProject_JAttackDefinition> RequestedAttack = ResolveAttack(Handle, Info);
	if (!NPC || !NPC->HasAuthority() || !Action || !Action->CanCommitToTarget(RequestedTarget.Get())
		|| !Action->PrepareForAttack(RequestedTarget.Get())
		|| !CommitAbility(Handle, Info, ActivationInfo))
	{
		EndAbility(Handle, Info, ActivationInfo, true, true); return;
	}
	// Cost/cooldown application may synchronously cancel the ability or change its owner/components.
	if (!IsActive()) { return; }
	if (!IsValid(NPC) || NPC->IsActorBeingDestroyed() || !IsValid(Action) || Action->IsBeingDestroyed()
		|| !RequestedTarget.IsValid() || !Action->CanCommitToTarget(RequestedTarget.Get())
		|| !RequestedAttack.IsValid() || RequestedAttack.Get() != ResolveAttack(Handle, Info)
		|| !RequestedAttack->Montage || !NPC->GetMesh() || !NPC->GetMesh()->GetAnimInstance()
		|| !NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>())
	{
		EndAbility(Handle, Info, ActivationInfo, true, true); return;
	}
	LockedTarget = RequestedTarget; ActiveDefinition = RequestedAttack.Get();
	const FName Failure = ValidateAttackContext(NPC, ActiveDefinition);
	if (!Failure.IsNone()) { LogAttackRejection(Failure); EndAbility(Handle, Info, ActivationInfo, true, true); return; }
	AttackCharacter = NPC;
	NPC->MovementModeChangedDelegate.AddDynamic(this, &ThisClass::OnMovementModeChanged);
	FRotator Facing = (LockedTarget->GetActorLocation() - NPC->GetActorLocation()).Rotation();
	Facing.Pitch = Facing.Roll = 0; NPC->SetActorRotation(Facing);
	AttackMesh = NPC->GetMesh();
	SavedVisibility = uint8(AttackMesh->VisibilityBasedAnimTickOption); bSavedURO = AttackMesh->bEnableUpdateRateOptimizations;
	bChangedMeshPolicy = true;
	// Dedicated servers must refresh bones and execute hit notifies even without a rendered mesh.
	AttackMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AttackMesh->bEnableUpdateRateOptimizations = false;
	NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>()->BeginAttackNode(ActiveDefinition->AttackTag, ActiveDefinition);
	if (auto* Presentation = NPC->FindComponentByClass<UProject_JCombatPresentationComponent>())
	{
		Presentation->BeginAttackPresentation(ActiveDefinition->AttackTag);
	}
	auto* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, HitEventTag, nullptr, false, true);
	HitTask->EventReceived.AddDynamic(this, &ThisClass::OnHit); HitTask->ReadyForActivation();
	auto* Montage = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None,
		ActiveDefinition->Montage, ActiveDefinition->PlayRate, ActiveDefinition->MontageSectionName);
	Montage->OnCompleted.AddDynamic(this, &ThisClass::OnCompleted);
	Montage->OnInterrupted.AddDynamic(this, &ThisClass::OnInterrupted);
	Montage->OnCancelled.AddDynamic(this, &ThisClass::OnInterrupted);
	Montage->ReadyForActivation();
	UE_CLOG(IsActive() && CVarNPCAttackDebug.GetValueOnGameThread() != 0, LogProjectJNPCAttack, Log,
		TEXT("Started Owner=%s Montage=%s RootMotion=%d"), *GetNameSafe(NPC), *GetNameSafe(RequestedAttack->Montage), RequestedAttack->Montage->HasRootMotion());
}

void UProject_JGameplayAbility_NPCAttack::OnMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	if (IsActive() && !bEndingAttack && Character == AttackCharacter.Get()
		&& (!Character->GetCharacterMovement() || !Character->GetCharacterMovement()->IsMovingOnGround()))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}
void UProject_JGameplayAbility_NPCAttack::OnHit(FGameplayEventData Payload)
{
	auto* NPC = Cast<AProject_JNPCCharacter>(GetAvatarActorFromActorInfo());
	auto* Target = LockedTarget.Get();
	const auto* Action = NPC ? NPC->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr;
	if (!IsActive() || !NPC || !NPC->HasAuthority() || !Target || Payload.Target.Get() != Target
		|| Payload.Instigator.Get() != NPC || !Payload.EventTag.MatchesTagExact(HitEventTag)
		|| !Action || !Action->CanCommitToTarget(Target)) { return; }
	if (auto* Hit = NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>())
	{
		if (Hit->GetActiveAttackDefinition() == ActiveDefinition) { Hit->ProcessAuthorityHit(Target); }
	}
}
void UProject_JGameplayAbility_NPCAttack::OnCompleted() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false); }
void UProject_JGameplayAbility_NPCAttack::OnInterrupted() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); }
void UProject_JGameplayAbility_NPCAttack::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive() || bEndingAttack) { return; }
	TGuardValue<bool> EndingGuard(bEndingAttack, true);
	if (auto* Character = AttackCharacter.Get()) { Character->MovementModeChangedDelegate.RemoveDynamic(this, &ThisClass::OnMovementModeChanged); }
	AttackCharacter.Reset();
	// Cancel only our montage, without a residual root-motion blend after action ownership ends.
	if (auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
		ASC && ActiveDefinition && ASC->GetAnimatingAbility() == this && ASC->GetCurrentMontage() == ActiveDefinition->Montage)
	{
		ASC->CurrentMontageStop(0.0f);
	}
	if (auto* Avatar = Info ? Info->AvatarActor.Get() : nullptr)
	{
		if (auto* Hit = Avatar->FindComponentByClass<UProject_JCombatHitValidationComponent>(); Hit && ActiveDefinition && Hit->GetActiveAttackDefinition() == ActiveDefinition) { Hit->EndAttack(); }
		if (auto* VFX = Avatar->FindComponentByClass<UProject_JCombatPresentationComponent>(); VFX && ActiveDefinition && VFX->GetActiveAttackTag() == ActiveDefinition->AttackTag) { VFX->EndAttackPresentation(); }
	}
	if (auto* Mesh = AttackMesh.Get(); Mesh && bChangedMeshPolicy)
	{
		if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption(SavedVisibility);
		}
		if (!Mesh->bEnableUpdateRateOptimizations) { Mesh->bEnableUpdateRateOptimizations = bSavedURO; }
	}
	bChangedMeshPolicy = false; AttackMesh.Reset(); LockedTarget.Reset(); ActiveDefinition = nullptr;
	UE_CLOG(CVarNPCAttackDebug.GetValueOnGameThread() != 0, LogProjectJNPCAttack, Log,
		TEXT("Ended Owner=%s Cancelled=%d"), *GetNameSafe(Info ? Info->AvatarActor.Get() : nullptr), bWasCancelled);
	Super::EndAbility(Handle, Info, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
