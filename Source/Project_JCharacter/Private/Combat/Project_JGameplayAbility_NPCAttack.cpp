#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Movement/Project_JMoverComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Project_JGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemGlobals.h"
#include "UObject/StrongObjectPtr.h"

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
	if (!Attack->GetMeleeTrajectoryFailure(NPC->GetMesh()->GetSkeletalMeshAsset()).IsEmpty()) { return TEXT("InvalidAuthoredMeleeTrajectory"); }
	if (!HitEventTag.IsValid()) { return TEXT("MissingHitEventTag"); }
	if (Attack->NPCMaxMeleeTargets < 1 || Attack->NPCMaxMeleeTargets > UProject_JAttackDefinition::MaxNPCMeleeTargets) { return TEXT("InvalidMeleeTargetLimit"); }
	if (!FMath::IsFinite(Attack->PlayRate) || Attack->PlayRate <= 0 || Attack->Montage->GetPlayLength() <= 0) { return TEXT("InvalidMontageRateOrLength"); }
	if (!Attack->MontageSectionName.IsNone() && Attack->Montage->GetSectionIndex(Attack->MontageSectionName) == INDEX_NONE) { return TEXT("MissingMontageSection"); }
	if (!SupportsMovementPolicy(*Attack)) { return TEXT("UnsupportedWarpedAerialOrInPlaceRootMotion"); }
	if (!Project_J::Movement::IsGrounded(NPC)) { return TEXT("NotGrounded"); }
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
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_NPCActivate);
	const uint64 ActivationRevision = ++ExecutionRevision;
	auto* NPC = Info ? Cast<AProject_JNPCCharacter>(Info->AvatarActor.Get()) : nullptr;
	auto* Action = NPC ? NPC->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr;
	const TWeakObjectPtr<AActor> RequestedTarget = Action ? Action->GetIntentTarget() : nullptr;
	const TWeakObjectPtr<UProject_JAttackDefinition> RequestedAttack = ResolveAttack(Handle, Info);
	if (!NPC || !NPC->HasAuthority() || !Action
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
	ActiveTargetLimit = ActiveDefinition->NPCMaxMeleeTargets;
	const FName Failure = ActiveTargetLimit < 1 || ActiveTargetLimit > UProject_JAttackDefinition::MaxNPCMeleeTargets
		? FName(TEXT("InvalidMeleeTargetLimit")) : ValidateAttackContext(NPC, ActiveDefinition);
	if (!Failure.IsNone()) { LogAttackRejection(Failure); EndAbility(Handle, Info, ActivationInfo, true, true); return; }
	AttackCharacter = NPC;
	NPC->GetMover()->OnPostFinalize.AddDynamic(this, &ThisClass::OnPhysicalFrameFinalized);
	FRotator Facing = (LockedTarget->GetActorLocation() - NPC->GetActorLocation()).Rotation();
	Facing.Pitch = Facing.Roll = 0;
	NPC->FaceGameplayDirection(Facing);
	// Shared hit lifetime protects mandatory pose/notifies for both NPC and player.
	NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>()->BeginAttackNode(ActiveDefinition->AttackTag, ActiveDefinition);
	if (auto* Presentation = NPC->FindComponentByClass<UProject_JCombatPresentationComponent>())
	{
		Presentation->BeginAttackPresentation(ActiveDefinition->AttackTag);
	}
	// An ended GameplayTask is marked garbage even while its EventReceived broadcast is on the stack.
	// ASC exact-event dispatch owns a delegate copy across cancellation and callback GC instead.
	if (!IsActive() || ExecutionRevision != ActivationRevision) { return; }
	const TStrongObjectPtr<UAbilitySystemComponent> EventSource(Info ? Info->AbilitySystemComponent.Get() : nullptr);
	if (!IsValid(EventSource.Get()) || EventSource->IsBeingDestroyed())
	{
		EndAbility(Handle, Info, ActivationInfo, true, true); return;
	}
	HitEventSource = EventSource.Get();
	RegisteredHitEventTag = HitEventTag;
	HitEventDelegate = EventSource->GenericGameplayEventCallbacks.FindOrAdd(RegisteredHitEventTag).AddUObject(
		this, &ThisClass::OnHitEvent, ActivationRevision, RegisteredHitEventTag);
	auto* Montage = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None,
		ActiveDefinition->Montage, ActiveDefinition->PlayRate, ActiveDefinition->MontageSectionName);
	Montage->OnCompleted.AddDynamic(this, &ThisClass::OnCompleted);
	Montage->OnInterrupted.AddDynamic(this, &ThisClass::OnInterrupted);
	Montage->OnCancelled.AddDynamic(this, &ThisClass::OnInterrupted);
	Montage->ReadyForActivation();
	UE_CLOG(IsActive() && CVarNPCAttackDebug.GetValueOnGameThread() != 0, LogProjectJNPCAttack, Log,
		TEXT("Started Owner=%s Montage=%s RootMotion=%d"), *GetNameSafe(NPC), *GetNameSafe(RequestedAttack->Montage), RequestedAttack->Montage->HasRootMotion());
}

void UProject_JGameplayAbility_NPCAttack::OnPhysicalFrameFinalized(const FMoverSyncState& State,const FMoverAuxStateContext&)
{
	const auto* NPC=AttackCharacter.Get();
	if (IsActive() && !bEndingAttack && (!NPC || !NPC->GetMover()->HasGameplayTagInState(State,Mover_IsOnGround,true)))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}
void UProject_JGameplayAbility_NPCAttack::OnHitEvent(const FGameplayEventData* Payload, uint64 ExpectedRevision, FGameplayTag RegisteredTag)
{
	// A copied old listener cannot deliver its event to a newly activated execution of this instance.
	if (!Payload || !IsActive() || bEndingAttack || ExecutionRevision != ExpectedRevision
		|| !RegisteredTag.MatchesTagExact(HitEventTag)) { return; }
	FGameplayEventData CurrentPayload = *Payload;
	CurrentPayload.EventTag = RegisteredTag; // Match the former exact WaitGameplayEvent task's payload contract.
	OnHit(MoveTemp(CurrentPayload));
}
void UProject_JGameplayAbility_NPCAttack::OnHit(FGameplayEventData Payload)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_NPCOnHit);
	check(IsInGameThread());
	if (!IsActive() || bEndingAttack) { return; }
	auto* NPCActor = Cast<AProject_JNPCCharacter>(GetAvatarActorFromActorInfo());
	auto* PrimaryActor = LockedTarget.Get();
	auto* TargetActor = const_cast<AActor*>(Payload.Target.Get());
	const int32 Limit = ActiveTargetLimit;
	auto* HitComponent = NPCActor ? NPCActor->FindComponentByClass<UProject_JCombatHitValidationComponent>() : nullptr;
	// Repeated notify contacts and a full target budget do not need pins or extensible life/LOS queries.
	if (!IsValid(NPCActor) || NPCActor->IsActorBeingDestroyed() || !NPCActor->HasAuthority() || !PrimaryActor
		|| Payload.Instigator.Get() != NPCActor || !Payload.EventTag.MatchesTagExact(HitEventTag)
		|| (Limit == 1 && TargetActor != PrimaryActor) || !IsValid(HitComponent)
		|| HitComponent->GetActiveAttackDefinition() != ActiveDefinition || !HitComponent->CanProcessAuthorityHit(TargetActor, Limit)) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AProject_JNPCCharacter> NPC(NPCActor);
	const TStrongObjectPtr<AActor> Primary(PrimaryActor), Target(TargetActor);
	// Player and noncombat NPC targets do not share a mandatory spatial revision.
	// Retire any NPC contact if an extensible query moves/rotates its victim.
	const FVector ContactLocation = Target->GetActorLocation();
	const FQuat ContactRotation = Target->GetActorQuat();
	const TStrongObjectPtr<UProject_JAttackDefinition> Definition(ActiveDefinition.Get());
	const TStrongObjectPtr<UProject_JNPCActionComponent> Action(NPC ? NPC->FindComponentByClass<UProject_JNPCActionComponent>() : nullptr);
	const TStrongObjectPtr<UProject_JCombatHitValidationComponent> Hit(HitComponent);
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr);
	const TWeakObjectPtr<AActor> ASCOwner(ASC ? ASC->GetOwnerActor() : nullptr);
	const TStrongObjectPtr<UAbilitySystemComponent> PrimaryASC(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Primary.Get()));
	const TStrongObjectPtr<UAbilitySystemComponent> TargetASC(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Get()));
	const TWeakObjectPtr<AActor> PrimaryASCOwner(PrimaryASC ? PrimaryASC->GetOwnerActor() : nullptr), TargetASCOwner(TargetASC ? TargetASC->GetOwnerActor() : nullptr);
	const TWeakObjectPtr<AActor> PrimaryASCAvatar(PrimaryASC ? PrimaryASC->GetAvatarActor() : nullptr), TargetASCAvatar(TargetASC ? TargetASC->GetAvatarActor() : nullptr);
	const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC.Get());
	const auto* PrimaryProjectASC = Cast<UProject_JAbilitySystemComponent>(PrimaryASC.Get());
	const auto* TargetProjectASC = Cast<UProject_JAbilitySystemComponent>(TargetASC.Get());
	const uint64 Revision = ExecutionRevision, ActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
	const uint64 PrimaryActorInfoRevision = PrimaryProjectASC ? PrimaryProjectASC->GetActorInfoRevision() : 0;
	const uint64 TargetActorInfoRevision = TargetProjectASC ? TargetProjectASC->GetActorInfoRevision() : 0;
	const uint64 SpatialRevision = Hit ? Hit->GetSpatialRevision() : 0;
	const auto IsRecipientCurrent = [](UAbilitySystemComponent* Captured, const TWeakObjectPtr<AActor>& Owner,
		const TWeakObjectPtr<AActor>& Avatar, const UProject_JAbilitySystemComponent* Project, uint64 Epoch)
	{
		if (!Captured) { return true; } // Preserve legacy non-ASC targets: the existing effect path returns false.
		const auto Matches = [](AActor* Current, const TWeakObjectPtr<AActor>& Expected)
		{ return Expected.IsExplicitlyNull() ? Current == nullptr : Expected.IsValid() && !Expected->IsActorBeingDestroyed() && Current == Expected.Get(); };
		return IsValid(Captured) && !Captured->IsBeingDestroyed() && (!Project || Project->GetActorInfoRevision() == Epoch)
			&& Matches(Captured->GetOwnerActor(), Owner) && Matches(Captured->GetAvatarActor(), Avatar);
	};
	const auto IsCurrent = [&]
	{
		return IsActive() && !bEndingAttack && ExecutionRevision == Revision && IsValid(Definition.Get()) && ActiveDefinition == Definition.Get()
			&& LockedTarget.Get() == Primary.Get() && IsValid(Primary.Get()) && !Primary->IsActorBeingDestroyed()
			&& Limit >= 1 && Limit <= UProject_JAttackDefinition::MaxNPCMeleeTargets && ActiveTargetLimit == Limit
			&& IsValid(NPC.Get()) && !NPC->IsActorBeingDestroyed() && NPC->HasAuthority() && GetAvatarActorFromActorInfo() == NPC.Get()
			&& GetWorld() && !GetWorld()->bIsTearingDown && NPC->GetWorld() == GetWorld()
			&& IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && NPC->GetAbilitySystemComponent() == ASC.Get() && ASC->GetAvatarActor() == NPC.Get()
			&& CurrentActorInfo && CurrentActorInfo->AbilitySystemComponent.Get() == ASC.Get()
			&& ASCOwner.IsValid() && !ASCOwner->IsActorBeingDestroyed() && ASC->GetOwnerActor() == ASCOwner.Get()
			&& (!ProjectASC || ProjectASC->GetActorInfoRevision() == ActorInfoRevision)
			&& IsRecipientCurrent(PrimaryASC.Get(), PrimaryASCOwner, PrimaryASCAvatar, PrimaryProjectASC, PrimaryActorInfoRevision)
			&& IsRecipientCurrent(TargetASC.Get(), TargetASCOwner, TargetASCAvatar, TargetProjectASC, TargetActorInfoRevision)
			// The opt-in cleave's GE consumers have canonical Health. A life query may have killed another participant.
			&& (Limit == 1 || (ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) > 0
				&& PrimaryASC && PrimaryASC->GetAvatarActor() == Primary.Get() && PrimaryASCOwner.IsValid()
				&& PrimaryASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) > 0
				&& TargetASC && TargetASC->GetAvatarActor() == Target.Get() && TargetASCOwner.IsValid()
				&& TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) > 0))
			&& IsValid(Action.Get()) && !Action->IsBeingDestroyed() && Action->IsRegistered() && Action->GetOwner() == NPC.Get()
			&& IsValid(Hit.Get()) && !Hit->IsBeingDestroyed() && Hit->IsRegistered() && Hit->GetOwner() == NPC.Get()
			&& Hit->GetActiveAttackDefinition() == Definition.Get() && Hit->GetSpatialRevision() == SpatialRevision
			&& IsValid(Target.Get()) && !Target->IsActorBeingDestroyed()
			&& Target->GetActorLocation() == ContactLocation && Target->GetActorQuat() == ContactRotation;
	};
	if (!IsCurrent()) { return; }
	if (Action->CanCommitSweepHit(Primary.Get(), Target.Get()) && IsCurrent())
	{
		// A PlayerState/provider swap may leave the old ASC tuple alive. Validate the actual recipient again.
		if (UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Primary.Get()) != PrimaryASC.Get()
			|| UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target.Get()) != TargetASC.Get()) { return; }
		if (!IsCurrent()) { return; }
		// Shared dedup reserves this victim before applying the effect, including reentrant gameplay events.
		Hit->ProcessAuthorityHit(Target.Get(), Limit);
	}
}
void UProject_JGameplayAbility_NPCAttack::OnCompleted() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false); }
void UProject_JGameplayAbility_NPCAttack::OnInterrupted() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); }
void UProject_JGameplayAbility_NPCAttack::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive() || bEndingAttack) { return; }
	TGuardValue<bool> EndingGuard(bEndingAttack, true);
	++ExecutionRevision;
	// Remove from the original ASC/tag, before montage/ability cleanup can reenter.
	const TWeakObjectPtr<UAbilitySystemComponent> RegisteredSource = HitEventSource;
	const FGameplayTag RegisteredTag = RegisteredHitEventTag;
	const FDelegateHandle RegisteredDelegate = HitEventDelegate;
	HitEventSource.Reset(); RegisteredHitEventTag = FGameplayTag(); HitEventDelegate.Reset();
	if (auto* Source = RegisteredSource.Get(); Source && RegisteredDelegate.IsValid())
	{
		if (auto* Listener = Source->GenericGameplayEventCallbacks.Find(RegisteredTag)) { Listener->Remove(RegisteredDelegate); }
	}
	ActiveTargetLimit = 1;
	if (auto* Character = AttackCharacter.Get()) { Character->GetMover()->OnPostFinalize.RemoveDynamic(this, &ThisClass::OnPhysicalFrameFinalized); }
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
	LockedTarget.Reset(); ActiveDefinition = nullptr;
	UE_CLOG(CVarNPCAttackDebug.GetValueOnGameThread() != 0, LogProjectJNPCAttack, Log,
		TEXT("Ended Owner=%s Cancelled=%d"), *GetNameSafe(Info ? Info->AvatarActor.Get() : nullptr), bWasCancelled);
	Super::EndAbility(Handle, Info, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
