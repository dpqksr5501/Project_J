#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JNPCCharacter.h"
#include "Project_JCombatInterface.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Optimization/Project_JRetryDelay.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Project_JAttributeSet.h"
#include "GameFramework/NavMovementInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPCAction, Log, All);
namespace
{
	TAutoConsoleVariable<int32> CVarNPCActionDebug(TEXT("ProjectJ.NPC.Action.Debug"), 0,
		TEXT("Logs NPC pursuit requests, stale paths and invalid context. 0: off, 1: on."), ECVF_Default);
}

UProject_JNPCActionComponent::UProject_JNPCActionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UProject_JNPCActionComponent::SupportsAttackAbility(const UGameplayAbility* Ability)
{
	if (!Ability) { return false; }
	const auto Policy = Ability->GetNetExecutionPolicy();
	return Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor
		&& (Policy == EGameplayAbilityNetExecutionPolicy::ServerOnly || Policy == EGameplayAbilityNetExecutionPolicy::ServerInitiated);
}

void UProject_JNPCActionComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
}

bool UProject_JNPCActionComponent::IsAttackSupported() const
{
	if (!AttackHandle.IsValid()) { return true; }
	const auto* ASC = AbilitySystem.Get();
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(AttackHandle) : nullptr;
	if (!Spec || !Spec->Ability || Spec->PendingRemove || Spec->RemoveAfterActivation) { return false; }
	return SupportsAttackAbility(Spec->Ability);
}

bool UProject_JNPCActionComponent::StartActions(UProject_JTargetScoringComponent* Scoring, FGameplayAbilitySpecHandle AttackAbility)
{ return StartContext(Scoring, AttackAbility, nullptr, nullptr, 0); }

bool UProject_JNPCActionComponent::StartFollowing(AActor* FollowOwner, UAbilitySystemComponent* OwnerASC, double FollowDistance, TFunction<void()> OnStopped)
{
	const bool bAlreadyFollowing = bEnabled;
	if (!StartContext(nullptr, {}, FollowOwner, OwnerASC, FollowDistance)) return false;
	if (!bAlreadyFollowing) FollowStopped = MoveTemp(OnStopped); // Idempotent start preserves the current lease notification.
	return true;
}

bool UProject_JNPCActionComponent::StartNavigationTo(FVector Destination, double AcceptanceRadius, TFunction<void(bool)> OnStopped)
{
 check(IsInGameThread());
 if (bEnabled || !StartContext(nullptr, {}, nullptr, nullptr, 0, &Destination, AcceptanceRadius)) return false;
 NavigationStopped = MoveTemp(OnStopped);
 return true;
}

bool UProject_JNPCActionComponent::StartContext(UProject_JTargetScoringComponent* Scoring, FGameplayAbilitySpecHandle AttackAbility,
	AActor* FollowOwner, UAbilitySystemComponent* OwnerASC, double FollowDistance, const FVector* FixedDestination, double AcceptanceRadius)
{
	check(IsInGameThread());
	const bool bFollow = FollowOwner != nullptr;
	const bool bLocation = FixedDestination != nullptr;
	const FVector Destination = bLocation ? *FixedDestination : FVector::ZeroVector;
	if (bLocation && (bFollow || Scoring || AttackAbility.IsValid() || OwnerASC || Destination.ContainsNaN() ||
		Destination.GetAbsMax() > 1.e9 || !FMath::IsFinite(AcceptanceRadius) || AcceptanceRadius < 1 || AcceptanceRadius > 2000)) return false;
	if (bEnabled) { return !bLocation && !bNavigatingLocation && bFollowingOwner == bFollow && ScoringComponent.Get() == Scoring && AttackHandle == AttackAbility
		&& (!bFollow || (FollowOwnerActor.Get() == FollowOwner && FollowOwnerASC.Get() == OwnerASC && FollowStopDistance == FollowDistance)) && IsContextValid(); }
	auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	auto* AI = NPC ? Cast<AAIController>(NPC->GetController()) : nullptr;
	if (bStopping || bEndingPlay || IsBeingDestroyed() || !IsRegistered() || !GetWorld() || GetWorld()->bIsTearingDown
		|| GetWorld()->GetNetMode() == NM_Client || !IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed()
		|| (!bFollow && !bLocation && (!IsValid(Scoring) || Scoring->GetOwner() != NPC || !Scoring->IsRegistered()))
		|| (bFollow && (Scoring || AttackAbility.IsValid() || !IsValid(FollowOwner) || FollowOwner == NPC || FollowOwner->IsActorBeingDestroyed()
			|| FollowOwner->GetWorld() != GetWorld() || !FollowOwner->HasAuthority()
			|| !IsValid(OwnerASC) || OwnerASC->IsBeingDestroyed() || OwnerASC->GetWorld() != GetWorld() || OwnerASC->GetAvatarActor() != FollowOwner || !IsValid(OwnerASC->GetOwnerActor())
			|| !FMath::IsFinite(FollowDistance) || FollowDistance < 50 || FollowDistance > 2000))
		|| !AI || !AI->GetPathFollowingComponent() || AI->GetPathFollowingComponent()->GetStatus() != EPathFollowingStatus::Idle
		|| (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning())
		|| (!bLocation && (!FMath::IsFinite(AttackRange) || AttackRange < 50 || AttackRange > 2000))
		|| !FMath::IsFinite(RepathDistance) || RepathDistance < 25 || RepathDistance > 1000
		|| !FMath::IsFinite(RetryInterval) || RetryInterval < 0.1 || RetryInterval > 5
		|| (!bLocation && (!FMath::IsFinite(DecisionLifetime) || DecisionLifetime < 0.5 || DecisionLifetime > 10))) { return false; }
	const auto* FollowProjectASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC);
	if (bFollow && FollowProjectASC && FollowProjectASC->IsActorInfoChanging()) return false;
	// Registration query is independent of whether a target has been produced yet.
	if (!bFollow && !bLocation && !Scoring->IsBatchedNPCDecisionRegistered()) { return false; }
	if (bFollow || bLocation)
	{
		TInlineComponentArray<UProject_JTargetScoringComponent*> ExistingScoring(NPC);
		for (auto* Existing : ExistingScoring) { if (Existing->IsBatchedNPCDecisionRegistered()) return false; }
	}
	TArray<UProject_JNPCActionComponent*> Peers;
	NPC->GetComponents(Peers);
	for (const auto* Peer : Peers) { if (Peer != this && (Peer->bEnabled || Peer->OwnershipToken || Peer->HasPendingResume())) { return false; } }
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AProject_JNPCCharacter> KeepNPC(NPC);
	const TStrongObjectPtr<AAIController> KeepAI(AI);
	const TStrongObjectPtr<UProject_JTargetScoringComponent> KeepScoring(Scoring);
	const TStrongObjectPtr<AActor> KeepFollow(FollowOwner);
	const TStrongObjectPtr<UAbilitySystemComponent> KeepFollowASC(OwnerASC);
	const uint64 StartRevision = ActionRevision;
	const TStrongObjectPtr<UAbilitySystemComponent> CapturedASC(NPC->GetAbilitySystemComponent());
	// The virtual ASC/life queries may stop or start another lease and collect.
	const auto StillStarting = [&]
	{
		return !bEnabled && !bStopping && !bEndingPlay && !IsBeingDestroyed() && IsRegistered() && ActionRevision == StartRevision &&
			GetWorld() && !GetWorld()->bIsTearingDown && GetOwner() == NPC && IsValid(NPC) && !NPC->IsActorBeingDestroyed() &&
			NPC->HasAuthority() && IsValid(AI) && !AI->IsActorBeingDestroyed() && NPC->GetController() == AI && AI->GetPawn() == NPC &&
			AI->GetPathFollowingComponent() && AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Idle &&
			(!AI->GetBrainComponent() || !AI->GetBrainComponent()->IsRunning());
	};
	if (!StillStarting()) return false;
	if (bLocation && (!CapturedASC || CapturedASC->GetOwnerActor() != NPC ||
		!NPC->FindComponentByInterface<INavMovementInterface>() || NPC->GetNavAgentLocation().ContainsNaN() ||
		NPC->GetNavAgentLocation().GetAbsMax() > 1.e9 || IProject_JCombatInterface::Execute_IsDead(NPC) || !StillStarting())) return false;
	if (!bFollow && !bLocation && (!IsValid(Scoring) || Scoring->IsBeingDestroyed() || !Scoring->IsRegistered() || !Scoring->IsBatchedNPCDecisionRegistered())) return false;
	if (bFollow && (!IsValid(FollowOwner) || FollowOwner->IsActorBeingDestroyed() || !IsValid(OwnerASC) || OwnerASC->IsBeingDestroyed() ||
		OwnerASC->GetAvatarActor() != FollowOwner || FollowOwner->GetWorld() != GetWorld())) return false;
	if (bFollow || bLocation)
	{
		TInlineComponentArray<UProject_JTargetScoringComponent*> ExistingScoring(NPC);
		for (auto* Existing : ExistingScoring) if (Existing->IsBatchedNPCDecisionRegistered()) return false;
	}
	Peers.Reset(); NPC->GetComponents(Peers);
	for (const auto* Peer : Peers) if (Peer != this && (Peer->bEnabled || Peer->OwnershipToken || Peer->HasPendingResume())) return false;
	AbilitySystem = CapturedASC.Get(); AttackHandle = AttackAbility;
	auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(AbilitySystem.Get());
	if (!AbilitySystem.IsValid() || AbilitySystem->GetAvatarActor() != NPC || !IsAttackSupported()
		|| (ProjectASC && ProjectASC->IsActorInfoChanging()))
	{
		AbilitySystem.Reset(); AttackHandle = {}; return false;
	}
	Paths = GetWorld()->GetSubsystem<UProject_JNPCPathSubsystem>();
	if (!Paths.IsValid()) { AbilitySystem.Reset(); AttackHandle = {}; return false; }
	auto* Scheduler = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
	if (!Scheduler || !Scheduler->RegisterAction(this)) { Paths.Reset(); AbilitySystem.Reset(); AttackHandle = {}; return false; }
	Controller = AI; ScoringComponent = Scoring;
	bNavigatingLocation = bLocation; NavigationDestination = Destination; NavigationAcceptanceRadius = bLocation ? AcceptanceRadius : 0;
	bFollowingOwner = bFollow; FollowOwnerActor = FollowOwner; FollowOwnerASC = OwnerASC;
	FollowASCOwner = OwnerASC ? OwnerASC->GetOwnerActor() : nullptr; FollowStopDistance = FollowDistance;
	if (Scoring)
	{
		Scoring->OnQueryCompleted.AddUniqueDynamic(this, &ThisClass::OnScored);
		ContextHandle = Scoring->OnContextInvalidated.AddUObject(this, &ThisClass::ClearIntent);
	}
	const TWeakObjectPtr<AAIController> RegisteredController(AI);
	const uint64 RegisteredAction = ActionRevision + 1;
	MoveHandle = AI->GetPathFollowingComponent()->OnRequestFinished.AddWeakLambda(this,
		[this, RegisteredController, RegisteredAction](FAIRequestID RequestId, const FPathFollowingResult& Result)
		{
			if (bEnabled && ActionRevision == RegisteredAction && RegisteredController.IsValid() && Controller == RegisteredController)
				OnMoveFinished(RequestId, Result);
		});
	AbilityEndedHandle = AbilitySystem->OnAbilityEnded.AddUObject(this, &ThisClass::OnAbilityEnded);
	AbilitySystemActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
	if (ProjectASC) ActorInfoChangingHandle = ProjectASC->OnActorInfoChanging.AddUObject(this, &ThisClass::OnActorInfoChanging);
	if (auto* FollowASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC))
		FollowActorInfoChangingHandle = FollowASC->OnActorInfoChanging.AddUObject(this, &ThisClass::OnFollowActorInfoChanging);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
	++ActionRevision;
	OwnershipToken = ++NextOwnershipToken;
	ClearPendingResume();
	bEnabled = true; State = EProjectJNPCActionState::Idle;
	NextPathTime = NextAttackTime = 0;
	if (bFollow) { IntentTarget = FollowOwner; ++IntentRevision; }
	if (bLocation) { IntentTarget.Reset(); ++IntentRevision; }
	return true;
}

bool UProject_JNPCActionComponent::IsContextValid() const
{
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AProject_JNPCCharacter> NPC(Cast<AProject_JNPCCharacter>(GetOwner()));
	const TStrongObjectPtr<AAIController> AI(Controller.Get());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(ScoringComponent.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(AbilitySystem.Get());
	const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC.Get());
	const uint64 Revision = ActionRevision;
	const auto IsCurrent = [&]
	{
		return bEnabled && !bEndingPlay && !IsBeingDestroyed() && ActionRevision == Revision && GetWorld() && !GetWorld()->bIsTearingDown
			&& GetWorld()->GetNetMode() != NM_Client && IsValid(NPC.Get()) && NPC->HasAuthority() && !NPC->IsActorBeingDestroyed()
			&& GetOwner() == NPC.Get() && IsValid(AI.Get()) && Controller.Get() == AI.Get() && !AI->IsActorBeingDestroyed()
			&& AI->GetPawn() == NPC.Get() && NPC->GetController() == AI.Get() && AI->GetPathFollowingComponent()
			&& (!AI->GetBrainComponent() || !AI->GetBrainComponent()->IsRunning())
			&& Paths.IsValid()
			&& (bNavigatingLocation ? (!bFollowingOwner && !ScoringComponent.IsValid() && !AttackHandle.IsValid() && !IntentTarget.IsValid() &&
				ASC && ASC->GetOwnerActor() == NPC.Get() && !NavigationDestination.ContainsNaN() && NavigationDestination.GetAbsMax() <= 1.e9 &&
				FMath::IsFinite(NavigationAcceptanceRadius) && NavigationAcceptanceRadius >= 1 && NavigationAcceptanceRadius <= 2000) : (bFollowingOwner
				? (FollowOwnerActor.IsValid() && IntentTarget == FollowOwnerActor && !FollowOwnerActor->IsActorBeingDestroyed()
					&& FollowOwnerActor->GetWorld() == GetWorld() && FollowOwnerActor->HasAuthority()
					&& FollowOwnerASC.IsValid() && !FollowOwnerASC->IsBeingDestroyed() && FollowASCOwner.IsValid() && !FollowASCOwner->IsActorBeingDestroyed()
					&& FollowOwnerASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) > 0
					&& FollowOwnerASC->GetOwnerActor() == FollowASCOwner.Get() && FollowOwnerASC->GetAvatarActor() == FollowOwnerActor.Get() && !AttackHandle.IsValid())
				: (IsValid(Scoring.Get()) && ScoringComponent.Get() == Scoring.Get() && Scoring->IsRegistered() && !Scoring->IsBeingDestroyed()
					&& Scoring->IsBatchedNPCDecisionRegistered())))
			&& IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && AbilitySystem.Get() == ASC.Get()
			&& (!ProjectASC || (!ProjectASC->IsActorInfoChanging() && ProjectASC->GetActorInfoRevision() == AbilitySystemActorInfoRevision))
			&& NPC->GetAbilitySystemComponent() == ASC.Get() && ASC->GetAvatarActor() == NPC.Get() && IsAttackSupported();
	};
	return IsCurrent() && !IProject_JCombatInterface::Execute_IsDead(NPC.Get()) && IsCurrent();
}

bool UProject_JNPCActionComponent::IsTargetValid() const
{
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> Target(IntentTarget.Get());
	const TStrongObjectPtr<AActor> Owner(GetOwner());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(ScoringComponent.Get());
	const uint64 Revision = IntentRevision, Action = ActionRevision;
	if (bNavigatingLocation)
	{
		const auto* Pawn = Cast<APawn>(Owner.Get());
		return IsContextValid() && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && GetOwner() == Pawn &&
			!Pawn->GetNavAgentLocation().ContainsNaN() && Pawn->GetNavAgentLocation().GetAbsMax() <= 1.e9 &&
			bEnabled && bNavigatingLocation && ActionRevision == Action && IntentRevision == Revision;
	}
	if (bFollowingOwner)
	{
		return IsContextValid() && IsValid(Target.Get()) && !Target->IsActorBeingDestroyed() && Target.Get() == FollowOwnerActor.Get()
			&& !Target->GetActorLocation().ContainsNaN() && !Owner->GetActorLocation().ContainsNaN()
			&& bEnabled && bFollowingOwner && ActionRevision == Action && IntentRevision == Revision && IntentTarget.Get() == Target.Get();
	}
	const uint64 Registration = Scoring ? Scoring->GetBatchRegistrationRevision() : 0;
	const auto* Scheduler = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr;
	return IsContextValid() && IsValid(Target.Get()) && !Target->IsActorBeingDestroyed() && IsValid(Owner.Get()) && Scheduler
		&& Scoring && ScoringComponent.Get() == Scoring.Get() && !Target->GetActorLocation().ContainsNaN() && !Owner->GetActorLocation().ContainsNaN()
		&& Scheduler->CanActOnTarget(Scoring.Get(), Target.Get())
		&& bEnabled && ActionRevision == Action && IntentRevision == Revision && IntentTarget.Get() == Target.Get()
		&& GetOwner() == Owner.Get() && IsValid(Owner.Get()) && !Owner->IsActorBeingDestroyed()
		&& IsValid(Target.Get()) && !Target->IsActorBeingDestroyed() && IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed()
		&& ScoringComponent.Get() == Scoring.Get() && Scoring->GetBatchRegistrationRevision() == Registration
		&& FMath::IsFinite(Scoring->Range) && Scoring->Range > 0
		&& FVector::DistSquared(Owner->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(Scoring->Range)
		&& FPlatformTime::Seconds() - LastDecision <= DecisionLifetime;
}

bool UProject_JNPCActionComponent::CanCommitToTarget(AActor* Target) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_NPCCommitTarget);
	if (bFollowingOwner || bNavigatingLocation) return false; // Non-hostile intents never grant combat authority.
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	const TStrongObjectPtr<AActor> Owner(GetOwner());
	const TStrongObjectPtr<AAIController> AI(Controller.Get());
	const uint64 Revision = IntentRevision, Action = ActionRevision;
	return IntentTarget.Get() == Target && IsTargetValid() && AI && Controller.Get() == AI.Get()
		&& GetOwner() == Owner.Get() && IsValid(Owner.Get()) && IsValid(Target) && !Target->IsActorBeingDestroyed()
		&& FVector::DistSquared(Owner->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(AttackRange)
		&& AI->LineOfSightTo(Target) && bEnabled && ActionRevision == Action && IntentRevision == Revision && IntentTarget.Get() == Target;
}

bool UProject_JNPCActionComponent::CanCommitSweepHit(AActor* PrimaryTarget, AActor* HitTarget) const
{
	check(IsInGameThread());
	if (bFollowingOwner || bNavigatingLocation) { return false; }
	if (HitTarget == PrimaryTarget) { return CanCommitToTarget(PrimaryTarget); }
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> Primary(PrimaryTarget), Target(HitTarget), Owner(GetOwner());
	const TStrongObjectPtr<AAIController> AI(Controller.Get());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(ScoringComponent.Get());
	const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> Scheduler(GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr);
	const uint64 Action = ActionRevision, Intent = IntentRevision;
	const uint64 Registration = Scoring ? Scoring->GetBatchRegistrationRevision() : 0;
	const uint64 TargetsRevision = Scheduler ? Scheduler->GetTargetRegistryRevision() : 0;
	const double Range = AttackRange;
	const auto IsCurrent = [&]
	{
		return bEnabled && !bFollowingOwner && !bEndingPlay && !IsBeingDestroyed() && ActionRevision == Action && IntentRevision == Intent
			&& IntentTarget.Get() == Primary.Get() && IsValid(Primary.Get()) && !Primary->IsActorBeingDestroyed() && Primary->GetWorld() == GetWorld()
			&& GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_Client
			&& IsValid(Owner.Get()) && !Owner->IsActorBeingDestroyed() && GetOwner() == Owner.Get() && Owner->HasAuthority()
			&& IsValid(Target.Get()) && !Target->IsActorBeingDestroyed() && Target->GetWorld() == GetWorld()
			&& IsValid(AI.Get()) && !AI->IsActorBeingDestroyed() && Controller.Get() == AI.Get()
			&& IsValid(Scoring.Get()) && !Scoring->IsBeingDestroyed() && ScoringComponent.Get() == Scoring.Get()
			&& Scoring->GetBatchRegistrationRevision() == Registration && IsValid(Scheduler.Get()) && Scheduler->GetTargetRegistryRevision() == TargetsRevision
			&& FMath::IsFinite(Range) && Range >= 50 && Range <= 2000 && AttackRange == Range;
	};
	// IsDead and controller queries are extensible. Revalidate identity and the primary after their callbacks.
	if (!IsCurrent() || !CanCommitToTarget(Primary.Get()) || !IsCurrent()
		|| !Scheduler->CanActOnTarget(Scoring.Get(), Target.Get()) || !IsCurrent()
		|| !CanCommitToTarget(Primary.Get()) || !IsCurrent()) { return false; }
	const FVector Origin = Owner->GetActorLocation(), PrimaryLocation = Primary->GetActorLocation(), Location = Target->GetActorLocation();
	if (Origin.ContainsNaN() || PrimaryLocation.ContainsNaN() || Location.ContainsNaN()
		|| FVector::DistSquared(Origin, PrimaryLocation) > FMath::Square(Range) || FVector::DistSquared(Origin, Location) > FMath::Square(Range)) { return false; }
	return AI->LineOfSightTo(Target.Get()) && IsCurrent() && Owner->GetActorLocation() == Origin
		&& Primary->GetActorLocation() == PrimaryLocation && Target->GetActorLocation() == Location;
}

bool UProject_JNPCActionComponent::HasForeignMovement() const
{
	const auto* AI = Controller.Get();
	const auto* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following && Following->GetStatus() != EPathFollowingStatus::Idle
		&& (!MoveId.IsValid() || Following->GetCurrentRequestId() != MoveId);
}

bool UProject_JNPCActionComponent::PrepareForAttack(AActor* Target)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_NPCPrepare);
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	const uint64 Revision = IntentRevision, Action = ActionRevision;
	if (!CanCommitToTarget(Target) || HasForeignMovement()) { return false; }
	CancelPathAndMove();
	return IntentRevision == Revision && ActionRevision == Action && CanCommitToTarget(Target) && !HasForeignMovement();
}

FVector UProject_JNPCActionComponent::GetTargetGoal() const
{
	if (bNavigatingLocation) return NavigationDestination;
	if (const auto* Pawn = Cast<APawn>(IntentTarget.Get())) { return Pawn->GetNavAgentLocation(); }
	return IntentTarget.IsValid() ? IntentTarget->GetActorLocation() : FVector::ZeroVector;
}

void UProject_JNPCActionComponent::OnScored(AActor* Target, double Score)
{
	ApplyScoredTarget(Target, true);
}

void UProject_JNPCActionComponent::ApplyScoredTarget(AActor* Target, bool bFreshDecision)
{
	check(IsInGameThread());
	if (bFollowingOwner || bNavigatingLocation) return;
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	const uint64 Action = ActionRevision, BeforeIntent = IntentRevision;
	if (!IsContextValid()) { if (ActionRevision == Action && IntentRevision == BeforeIntent) { StopActions(); } return; }
	if (ActionRevision != Action || IntentRevision != BeforeIntent) { return; }
	const auto HasActiveOwnedAttack = [&]
	{
		const auto* ASC = AbilitySystem.Get();
		const auto* Spec = bOwnsAttack && ASC ? ASC->FindAbilitySpecFromHandle(AttackHandle) : nullptr;
		return Spec && Spec->IsActive();
	};
	if (HasActiveOwnedAttack())
	{
		// Ranking is advisory for the next action. A live, valid GAS-owned attack
		// retains its captured intent; authority/life/membership/range still revalidate.
		const bool bValidIntent = IsTargetValid();
		if (ActionRevision != Action || IntentRevision != BeforeIntent || !bEnabled) { return; }
		if (HasActiveOwnedAttack() && bValidIntent)
		{
			if (bFreshDecision) { LastDecision = FPlatformTime::Seconds(); }
			return;
		}
	}
	if (Target != IntentTarget.Get())
	{
		ClearIntent();
		// A non-cancellable old attack must not silently read a new target from this component.
		if (!bEnabled || bOwnsAttack || ActionRevision != Action || IntentRevision != BeforeIntent + 1) { return; }
		IntentTarget = Target;
	}
	if (bFreshDecision) { LastDecision = FPlatformTime::Seconds(); }
	const uint64 CurrentIntent = IntentRevision;
	if (!IsTargetValid() && ActionRevision == Action && IntentRevision == CurrentIntent) { ClearIntent(); }
}

void UProject_JNPCActionComponent::CancelPathAndMove()
{
	const uint64 Token = PathToken; PathToken = 0;
	bCanRefreshQueuedPath = false;
	QueuedGoal = FVector::ZeroVector;
	if (auto* Service = Paths.Get()) { Service->Cancel(Token); }
	CancelMove();
}

void UProject_JNPCActionComponent::CancelMove()
{
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const FAIRequestID Previous = MoveId; MoveId = FAIRequestID::InvalidRequest;
	if (auto* AI = Controller.Get())
	{
		const TStrongObjectPtr<AAIController> PinnedAI(AI);
		const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
		auto* Following = AI->GetPathFollowingComponent();
		const TStrongObjectPtr<UPathFollowingComponent> PinnedFollowing(Following);
		if (Following && Previous.IsValid() && Following->GetCurrentRequestId() == Previous)
		{
			Following->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Previous);
		}
	}
}

void UProject_JNPCActionComponent::CancelOwnedAttack()
{
	if (!bOwnsAttack) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(AbilitySystem.Get());
	if (IsValid(ASC.Get()) && !ASC->IsBeingDestroyed()) { ASC->CancelAbilityHandle(AttackHandle); }
	// Non-cancellable ability phases remain GAS-owned; do not force EndAbility or cancel unrelated grants.
}

void UProject_JNPCActionComponent::ClearIntent()
{
	check(IsInGameThread());
	if (bEnabled && bNavigatingLocation) { FinishNavigation(false); return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const uint64 Revision = ++IntentRevision, Action = ActionRevision;
	IntentTarget.Reset();
	PathFailures = 0; NextPathTime = 0;
	CancelPathAndMove();
	if (IntentRevision != Revision || ActionRevision != Action) { return; }
	CancelOwnedAttack();
	if (IntentRevision == Revision && ActionRevision == Action) { State = bEnabled ? EProjectJNPCActionState::Idle : EProjectJNPCActionState::Disabled; }
}

void UProject_JNPCActionComponent::UpdateAction()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCAction_Update);
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(IntentTarget.Get());
	const TStrongObjectPtr<AAIController> PinnedController(Controller.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> PinnedASC(AbilitySystem.Get());
	const uint64 Action = ActionRevision, Intent = IntentRevision;
	const auto IsCurrent = [&] { return ActionRevision == Action && IntentRevision == Intent && !bEndingPlay && !IsBeingDestroyed(); };
	const bool bContextValid = IsContextValid();
	if (!IsCurrent()) { return; }
	if (!bContextValid || HasForeignMovement())
	{
		UE_CLOG(CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
			TEXT("Stopped Owner=%s ContextValid=%d ForeignMove=%d"), *GetNameSafe(GetOwner()), bContextValid, HasForeignMovement());
		StopActions(); return;
	}
	if (!bFollowingOwner && !bNavigatingLocation && !bOwnsAttack && State == EProjectJNPCActionState::Attacking)
	{
		// OnAbilityEnded only releases GAS ownership. Before another attack, consume
		// the current advisory selection without manufacturing a fresh receipt.
		// A pending query exposes nullptr; its ordinary completion will supply the next intent.
		const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(ScoringComponent.Get());
		ApplyScoredTarget(Scoring ? Scoring->GetLastScoredTarget() : nullptr, false);
		if (!IsCurrent()) { return; }
	}
	if (!IsTargetValid())
	{
		if (!IsCurrent()) { return; }
		UE_CLOG(IntentTarget.IsValid() && CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
			TEXT("Intent invalid Owner=%s Target=%s Distance=%.1f Range=%.1f Age=%.2f"), *GetNameSafe(GetOwner()),
			*GetNameSafe(IntentTarget.Get()), IntentTarget.IsValid() ? FVector::Dist(GetOwner()->GetActorLocation(), IntentTarget->GetActorLocation()) : 0,
			ScoringComponent.IsValid() ? ScoringComponent->Range : FollowStopDistance, FPlatformTime::Seconds() - LastDecision);
		ClearIntent(); return;
	}
	if (!IsCurrent()) { return; }
	const double Now = FPlatformTime::Seconds();
	if (bOwnsAttack) { State = EProjectJNPCActionState::Attacking; return; }
	const FVector Delta = bNavigatingLocation ? NavigationDestination - CastChecked<APawn>(GetOwner())->GetNavAgentLocation() :
		IntentTarget->GetActorLocation() - GetOwner()->GetActorLocation();
	if (!IsCurrent()) return;
	const bool bReached = bNavigatingLocation ? HasReachedNavigationGoal() : (Delta.SizeSquared() <= FMath::Square(bFollowingOwner ? FollowStopDistance : AttackRange)
		&& (bFollowingOwner || Controller->LineOfSightTo(IntentTarget.Get())));
	if (!IsCurrent()) return;
	if (bReached)
	{
		if (bNavigatingLocation) { ++Stats.Arrivals; FinishNavigation(true); return; }
		CancelPathAndMove();
		if (!IsCurrent()) { return; }
		if (!IsTargetValid()) { if (IsCurrent()) { ClearIntent(); } return; }
		if (!IsCurrent()) { return; }
		if (State != EProjectJNPCActionState::InRange && State != EProjectJNPCActionState::Attacking) { ++Stats.Arrivals; }
		State = EProjectJNPCActionState::InRange;
		if (!AttackHandle.IsValid() || Now < NextAttackTime) { return; }
		auto* Spec = AbilitySystem->FindAbilitySpecFromHandle(AttackHandle);
		if (!Spec || Spec->IsActive()) { return; }
		NextAttackTime = Now + RetryInterval;
		// Set ownership before activation: instant abilities can end synchronously in TryActivateAbility.
		const uint64 Revision = IntentRevision;
		bOwnsAttack = true;
		const bool Activated = AbilitySystem->TryActivateAbility(AttackHandle, false);
		if (!IsCurrent())
		{
			if (ActionRevision == Action) { CancelOwnedAttack(); }
			return;
		}
		if (!Activated) { bOwnsAttack = false; }
		if (!bEnabled || IntentRevision != Revision) { CancelOwnedAttack(); return; }
		if (bOwnsAttack) { State = EProjectJNPCActionState::Attacking; }
		return;
	}
	// Leaving attack range invalidates the old arrival state immediately, even
	// while the next path request is waiting for its rate limit or admission slot.
	if (State == EProjectJNPCActionState::InRange)
	{
		State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath
			: (PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff);
	}
	// A different system's active attack is not ours to cancel or move through.
	if (AttackHandle.IsValid())
	{
		const auto* Spec = AbilitySystem->FindAbilitySpecFromHandle(AttackHandle);
		if (Spec && Spec->IsActive()) { CancelPathAndMove(); if (IsCurrent()) { State = EProjectJNPCActionState::InRange; } return; }
	}
	const FVector Goal = GetTargetGoal();
	if (!IsCurrent()) return;
	if (Goal.ContainsNaN() || Goal.GetAbsMax() > 1.e9) { ClearIntent(); return; }
	if (MoveId.IsValid())
	{
		const auto* Following = Controller->GetPathFollowingComponent();
		const auto Path = Following->GetPath();
		if (Following->GetStatus() == EPathFollowingStatus::Idle || !Paths->IsPathUsable(Path))
		{
			// A pending replacement remains useful even if the old path has become invalid.
			CancelMove();
			if (!IsCurrent()) { return; }
			if (bEnabled) { State = PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff; }
		}
	}
	if (!IsTargetValid()) { if (IsCurrent()) { ClearIntent(); } return; }
	if (!IsCurrent()) { return; }
	if (PathToken)
	{
		if (bCanRefreshQueuedPath && FVector::DistSquared(Goal, QueuedGoal) > FMath::Square(RepathDistance))
		{
			bCanRefreshQueuedPath = Paths->RefreshQueuedGoal(PathToken, this, IntentRevision, Goal);
			if (bCanRefreshQueuedPath) { QueuedGoal = Goal; }
		}
		return; // A dispatched query remains immutable and keeps its physical slot.
	}
	if (Now < NextPathTime) { return; }
	if (MoveId.IsValid() && FVector::DistSquared(Goal, FollowingGoal) <= FMath::Square(RepathDistance)) { return; }
	// Keep the valid old path until a replacement is ready. Target motion does not repeatedly cancel in-flight work.
	NextPathTime = Now + RetryInterval;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	++Stats.PathRequests;
	PathToken = Paths->Submit(this, CastChecked<APawn>(GetOwner()), Goal, IntentRevision,
		[WeakThis](const FProjectJNPCPathCompletion& Completion)
		{
			if (auto* Self = WeakThis.Get()) { Self->OnPathReady(Completion); }
		}, MoveId.IsValid() ? EProjectJNPCPathPriority::Normal : EProjectJNPCPathPriority::Urgent);
	QueuedGoal = PathToken ? Goal : FVector::ZeroVector;
	bCanRefreshQueuedPath = PathToken != 0;
	if (!PathToken) { ++Stats.PathRejected; BackoffPath(); }
	State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath
		: (PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff);
	UE_CLOG(CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
		TEXT("Path requested Owner=%s Token=%llu Following=%d Goal=%s"), *GetNameSafe(GetOwner()), PathToken, MoveId.IsValid(), *Goal.ToString());
}

void UProject_JNPCActionComponent::OnPathReady(const FProjectJNPCPathCompletion& Completion)
{
	check(IsInGameThread());
	if (!bEnabled || PathToken != Completion.Token || IntentRevision != Completion.IntentRevision) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(IntentTarget.Get());
	const TStrongObjectPtr<AAIController> PinnedController(Controller.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> PinnedASC(AbilitySystem.Get());
	const uint64 Action = ActionRevision, Intent = IntentRevision;
	const auto IsCurrent = [&] { return bEnabled && ActionRevision == Action && IntentRevision == Intent && !bEndingPlay && !IsBeingDestroyed(); };
	PathToken = 0;
	bCanRefreshQueuedPath = false;
	QueuedGoal = FVector::ZeroVector;
	if (!IsContextValid() || HasForeignMovement()) { if (IsCurrent()) { StopActions(); } return; }
	if (!IsCurrent()) { return; }
	if (!IsTargetValid()) { if (IsCurrent()) { ClearIntent(); } return; }
	if (!IsCurrent()) { return; }
	NextPathTime = FPlatformTime::Seconds() + RetryInterval;
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	const auto* Spec = AttackHandle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(AttackHandle) : nullptr;
	if (Spec && Spec->IsActive()) { State = EProjectJNPCActionState::InRange; return; }
	const FVector CurrentGoal = GetTargetGoal();
	if (!IsCurrent()) return;
	const FVector CurrentStart = Pawn->GetNavAgentLocation();
	if (!IsCurrent()) return;
	if (Completion.Status != EProjectJNPCPathStatus::Success || !Paths->IsPathUsable(Completion.Path)
		|| CurrentGoal.ContainsNaN() || CurrentStart.ContainsNaN()
		|| FVector::DistSquared(CurrentGoal, Completion.Goal) > FMath::Square(RepathDistance)
		|| FVector::DistSquared(CurrentStart, Completion.Start) > FMath::Square(RepathDistance))
	{
		UE_CLOG(CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
			TEXT("Path discarded Owner=%s Status=%d StartDrift=%.1f GoalDrift=%.1f"), *GetNameSafe(GetOwner()), int32(Completion.Status),
			FVector::Dist(CurrentStart, Completion.Start), FVector::Dist(CurrentGoal, Completion.Goal));
		if (Completion.Status != EProjectJNPCPathStatus::Success) { ++Stats.PathFailed; if (!bNavigatingLocation) BackoffPath(); }
		else { ++Stats.PathStale; }
		if (bNavigatingLocation) { FinishNavigation(false); return; }
		State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath : EProjectJNPCActionState::Backoff; return;
	}
	FAIMoveRequest Move;
	Move.SetGoalLocation(Completion.Goal);
	Move.SetAcceptanceRadius(bNavigatingLocation ? float(NavigationAcceptanceRadius) : 25.0f).SetReachTestIncludesAgentRadius(false).SetReachTestIncludesGoalRadius(false);
	// RequestMove consumes our completed path. MoveTo would synchronously find another one.
	const uint64 Revision = IntentRevision;
	const TWeakObjectPtr<AAIController> RequestController = Controller;
	FAIRequestID StartedMove;
	{
		// RequestMove synchronously finishes the old move with Aborted|NewRequest.
		// That intentional replacement must not schedule failure backoff for a successful new path.
		TGuardValue<FAIRequestID> ReplacingGuard(ReplacingMoveId, MoveId);
		StartedMove = Controller->RequestMove(Move, Completion.Path);
	}
	if (!IsCurrent() || IntentRevision != Revision)
	{
		if (auto* AI = RequestController.Get())
		{
			auto* Following = AI->GetPathFollowingComponent();
			if (Following && StartedMove.IsValid() && Following->GetCurrentRequestId() == StartedMove)
			{
				Following->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, StartedMove);
			}
		}
		return;
	}
	MoveId = StartedMove;
	if (bNavigatingLocation && !MoveId.IsValid()) { FinishNavigation(false); return; }
	if (MoveId.IsValid()) { PathFailures = 0; } else { BackoffPath(); }
	FollowingGoal = Completion.Goal;
	State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath : EProjectJNPCActionState::Backoff;
}

void UProject_JNPCActionComponent::OnMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (!bEnabled || !MoveId.IsValid() || MoveId != RequestId) { return; }
	if (ReplacingMoveId == RequestId && Result.Code == EPathFollowingResult::Aborted
		&& (Result.Flags & FPathFollowingResultFlags::NewRequest) != 0)
	{
		++Stats.ReplacedMoves;
		return;
	}
	MoveId = FAIRequestID::InvalidRequest;
	if (bNavigatingLocation)
	{
		const uint64 Action = ActionRevision, Intent = IntentRevision;
		const bool bReached = Result.IsSuccess() && HasReachedNavigationGoal();
		if (!bEnabled || ActionRevision != Action || IntentRevision != Intent) return;
		if (!bReached) { FinishNavigation(false); return; }
	}
	if (Result.IsSuccess()) { PathFailures = 0; NextPathTime = FPlatformTime::Seconds() + RetryInterval; }
	else { BackoffPath(); }
	State = PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff;
}
void UProject_JNPCActionComponent::OnAbilityEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle == AttackHandle) { bOwnsAttack = false; }
}

void UProject_JNPCActionComponent::BackoffPath()
{
	++Stats.RetryScheduled;
	NextPathTime = FPlatformTime::Seconds() + ProjectJ::RetryDelay(PathFailures, GetUniqueID(), RetryInterval, 5.0);
	PathFailures = FMath::Min(PathFailures + 1, 5u);
}

bool UProject_JNPCActionComponent::HasReachedNavigationGoal() const
{
 const TStrongObjectPtr<AAIController> AI(Controller.Get());
 const TStrongObjectPtr<UPathFollowingComponent> Following(AI ? AI->GetPathFollowingComponent() : nullptr);
 const TStrongObjectPtr<APawn> Pawn(Cast<APawn>(GetOwner()));
 if (!bNavigatingLocation || !Pawn || !Following) return false;
 const uint64 Action = ActionRevision, Intent = IntentRevision;
 const FVector Origin = Pawn->GetNavAgentLocation();
 return bEnabled && bNavigatingLocation && ActionRevision == Action && IntentRevision == Intent &&
  IsValid(Pawn.Get()) && !Pawn->IsActorBeingDestroyed() && GetOwner() == Pawn.Get() && Controller.Get() == AI.Get() &&
  AI->GetPathFollowingComponent() == Following.Get() && Pawn->FindComponentByInterface<INavMovementInterface>() &&
  !Origin.ContainsNaN() && Origin.GetAbsMax() <= 1.e9 &&
  Following->HasReached(NavigationDestination, EPathFollowingReachMode::ExactLocation, float(NavigationAcceptanceRadius));
}

void UProject_JNPCActionComponent::FinishNavigation(bool bArrived)
{
 if (!bEnabled || !bNavigatingLocation || bStopping) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 const TStrongObjectPtr<UAbilitySystemComponent> KeepASC(AbilitySystem.Get());
 const uint64 ActorInfo = AbilitySystemActorInfoRevision;
 auto Stopped = MoveTemp(NavigationStopped);
 StopActions();
 const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(KeepASC.Get());
 const bool Arrived = bArrived && !bEndingPlay && !IsBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown &&
  IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed() && KeepASC && KeepASC->GetAvatarActor() == KeepOwner.Get() &&
  (!ProjectASC || (!ProjectASC->IsActorInfoChanging() && ProjectASC->GetActorInfoRevision() == ActorInfo));
 if (Stopped && !IsGarbageCollecting()) Stopped(Arrived);
}

void UProject_JNPCActionComponent::StopActions()
{
 check(IsInGameThread());
 if (bStopping) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
 auto Stopped = MoveTemp(NavigationStopped);
 StopActionState();
 if (Stopped && !IsGarbageCollecting()) Stopped(false);
}

void UProject_JNPCActionComponent::StopActionState()
{
	check(IsInGameThread());
	if (bStopping) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	TGuardValue<bool> Guard(bStopping, true);
	auto Stopped = MoveTemp(FollowStopped); // Detach the old lease before any cancellation callback.
	++ActionRevision;
	OwnershipToken = 0;
	ClearPendingResume();
	bEnabled = false; SetComponentTickEnabled(false);
	if (auto* Scheduler = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr)
	{
		Scheduler->UnregisterAction(this);
	}
	if (auto* Scoring = ScoringComponent.Get())
	{
		Scoring->OnQueryCompleted.RemoveDynamic(this, &ThisClass::OnScored);
		Scoring->OnContextInvalidated.Remove(ContextHandle);
	}
	// Detach before ability/path callbacks can replace GAS actor info or collect this owner.
	if (auto* ASC = Cast<UProject_JAbilitySystemComponent>(AbilitySystem.Get()))
		ASC->OnActorInfoChanging.Remove(ActorInfoChangingHandle);
	if (auto* ASC = Cast<UProject_JAbilitySystemComponent>(FollowOwnerASC.Get()))
		ASC->OnActorInfoChanging.Remove(FollowActorInfoChangingHandle);
	ActorInfoChangingHandle.Reset(); FollowActorInfoChangingHandle.Reset();
	ClearIntent();
	if (auto* AI = Controller.Get())
	{
		if (auto* Following = AI->GetPathFollowingComponent()) { Following->OnRequestFinished.Remove(MoveHandle); }
	}
	if (auto* ASC = AbilitySystem.Get()) { ASC->OnAbilityEnded.Remove(AbilityEndedHandle); }
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	bOwnsAttack = false;
	ScoringComponent.Reset(); Paths.Reset(); Controller.Reset(); AbilitySystem.Reset(); AttackHandle = {};
	AbilitySystemActorInfoRevision = 0;
	bNavigatingLocation = false; NavigationDestination = FVector::ZeroVector; NavigationAcceptanceRadius = 0;
	bFollowingOwner = false; FollowOwnerActor.Reset(); FollowOwnerASC.Reset(); FollowASCOwner.Reset(); FollowStopDistance = 0;
	if (Stopped && !IsGarbageCollecting()) Stopped();
}

void UProject_JNPCActionComponent::ClearPendingResume()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ResumeTimer); }
	PendingResume = {};
	ResumeAttempts = 0;
}

bool UProject_JNPCActionComponent::StopActionsIfOwned(uint64 Token)
{
	if (!Token || Token != OwnershipToken) { return false; }
	StopActions();
	return true;
}

bool UProject_JNPCActionComponent::SuspendActions(FProjectJNPCActionSuspension& OutSuspension)
{
	OutSuspension = {};
	if (!CanSuspendMovement() || bEndingPlay || HasPendingResume()) { return false; }
	const FProjectJNPCActionSuspension Before {ScoringComponent, AttackHandle, ActionRevision + 1, OwnershipToken, AbilitySystem, AbilitySystemActorInfoRevision};
	StopActions();
	if (ActionRevision != Before.Revision || bEnabled || bEndingPlay) { return false; }
	OutSuspension = Before;
	OwnershipToken = Before.OwnershipToken;
	return true;
}

EProjectJNPCActionResume UProject_JNPCActionComponent::ResumeSuspendedActions(const FProjectJNPCActionSuspension& Suspension)
{
	check(IsInGameThread());
	if (!Suspension.IsValid() || Suspension.Revision != ActionRevision || bEnabled || bEndingPlay || IsBeingDestroyed()
		|| !GetWorld() || GetWorld()->bIsTearingDown || !Suspension.Scoring.IsValid()
		|| !Suspension.Scoring->IsBatchedNPCDecisionRegistered()) { return EProjectJNPCActionResume::Superseded; }
	const auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	const auto* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
	const auto* ProjectASC = Cast<UProject_JAbilitySystemComponent>(ASC);
	if (!IsValid(ASC) || ASC->IsBeingDestroyed() || Suspension.AbilitySystem.Get() != ASC || ASC->GetAvatarActor() != NPC
		|| (ProjectASC && (ProjectASC->IsActorInfoChanging() || ProjectASC->GetActorInfoRevision() != Suspension.ActorInfoRevision)))
	{
		StopActionsIfOwned(Suspension.OwnershipToken);
		return EProjectJNPCActionResume::Superseded;
	}
	if (StartActions(Suspension.Scoring.Get(), Suspension.Attack))
	{
		OwnershipToken = Suspension.OwnershipToken;
		return EProjectJNPCActionResume::Resumed;
	}
	// Keep the original revision: an explicit Start/Stop from another owner supersedes this recovery.
	if (!PendingResume.IsValid())
	{
		PendingResume = Suspension;
		ResumeAttempts = 0;
		++Stats.ResumeDeferred;
		GetWorld()->GetTimerManager().SetTimer(ResumeTimer, this, &ThisClass::RetrySuspendedActions, 0.25f, true);
	}
	return EProjectJNPCActionResume::Deferred;
}

void UProject_JNPCActionComponent::RetrySuspendedActions()
{
	const FProjectJNPCActionSuspension Suspension = PendingResume;
	++ResumeAttempts;
	const auto Result = ResumeSuspendedActions(Suspension);
	if (Result != EProjectJNPCActionResume::Deferred) { ClearPendingResume(); }
	else if (ResumeAttempts >= 8)
	{
		++Stats.ResumeExhausted;
		UE_LOG(LogProjectJNPCAction, Warning, TEXT("Suspended NPC actions could not resume after bounded retries. Owner=%s"), *GetNameSafe(GetOwner()));
		ClearPendingResume();
	}
}
void UProject_JNPCActionComponent::OnTearDown(UWorld* World) { if (World == GetWorld()) { StopActions(); } }
void UProject_JNPCActionComponent::OnActorInfoChanging(AActor*, AActor*) { StopActions(); }
void UProject_JNPCActionComponent::OnFollowActorInfoChanging(AActor* NextOwner, AActor* NextAvatar)
{
	// Normal mount/dismount refreshes the rider's GAS metadata with the same recipient.
	// A clear or actual recipient change retires this lease even if a callback restores the old pointers.
	if (NextOwner != FollowASCOwner.Get() || NextAvatar != FollowOwnerActor.Get()) StopActions();
}
void UProject_JNPCActionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true; StopActions(); Super::EndPlay(Reason);
}
void UProject_JNPCActionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true; StopActions(); Super::OnComponentDestroyed(bDestroyingHierarchy);
}
