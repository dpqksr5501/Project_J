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
#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Optimization/Project_JRetryDelay.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"

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
{
	check(IsInGameThread());
	if (bEnabled) { return ScoringComponent.Get() == Scoring && AttackHandle == AttackAbility; }
	auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	auto* AI = NPC ? Cast<AAIController>(NPC->GetController()) : nullptr;
	if (bStopping || bEndingPlay || IsBeingDestroyed() || !IsRegistered() || !GetWorld() || GetWorld()->bIsTearingDown
		|| GetWorld()->GetNetMode() == NM_Client || !IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed()
		|| !IsValid(Scoring) || Scoring->GetOwner() != NPC || !Scoring->IsRegistered()
		|| !AI || !AI->GetPathFollowingComponent() || AI->GetPathFollowingComponent()->GetStatus() != EPathFollowingStatus::Idle
		|| (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning())
		|| !FMath::IsFinite(AttackRange) || AttackRange < 50 || AttackRange > 2000
		|| !FMath::IsFinite(RepathDistance) || RepathDistance < 25 || RepathDistance > 1000
		|| !FMath::IsFinite(RetryInterval) || RetryInterval < 0.1 || RetryInterval > 5
		|| !FMath::IsFinite(DecisionLifetime) || DecisionLifetime < 0.5 || DecisionLifetime > 10) { return false; }
	// Registration query is independent of whether a target has been produced yet.
	if (!Scoring->IsBatchedNPCDecisionRegistered()) { return false; }
	TArray<UProject_JNPCActionComponent*> Peers;
	NPC->GetComponents(Peers);
	for (const auto* Peer : Peers) { if (Peer != this && (Peer->bEnabled || Peer->OwnershipToken || Peer->HasPendingResume())) { return false; } }
	AbilitySystem = NPC->GetAbilitySystemComponent(); AttackHandle = AttackAbility;
	if (!AbilitySystem.IsValid() || AbilitySystem->GetAvatarActor() != NPC || !IsAttackSupported())
	{
		AbilitySystem.Reset(); AttackHandle = {}; return false;
	}
	Paths = GetWorld()->GetSubsystem<UProject_JNPCPathSubsystem>();
	if (!Paths.IsValid()) { AbilitySystem.Reset(); AttackHandle = {}; return false; }
	auto* Scheduler = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
	if (!Scheduler || !Scheduler->RegisterAction(this)) { Paths.Reset(); AbilitySystem.Reset(); AttackHandle = {}; return false; }
	Controller = AI; ScoringComponent = Scoring;
	Scoring->OnQueryCompleted.AddUniqueDynamic(this, &ThisClass::OnScored);
	ContextHandle = Scoring->OnContextInvalidated.AddUObject(this, &ThisClass::ClearIntent);
	MoveHandle = AI->GetPathFollowingComponent()->OnRequestFinished.AddUObject(this, &ThisClass::OnMoveFinished);
	AbilityEndedHandle = AbilitySystem->OnAbilityEnded.AddUObject(this, &ThisClass::OnAbilityEnded);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
	++ActionRevision;
	OwnershipToken = ++NextOwnershipToken;
	ClearPendingResume();
	bEnabled = true; State = EProjectJNPCActionState::Idle;
	NextPathTime = NextAttackTime = 0;
	return true;
}

bool UProject_JNPCActionComponent::IsContextValid() const
{
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AProject_JNPCCharacter> NPC(Cast<AProject_JNPCCharacter>(GetOwner()));
	const TStrongObjectPtr<AAIController> AI(Controller.Get());
	const TStrongObjectPtr<UProject_JTargetScoringComponent> Scoring(ScoringComponent.Get());
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(AbilitySystem.Get());
	const uint64 Revision = ActionRevision;
	const auto IsCurrent = [&]
	{
		return bEnabled && !bEndingPlay && !IsBeingDestroyed() && ActionRevision == Revision && GetWorld() && !GetWorld()->bIsTearingDown
			&& GetWorld()->GetNetMode() != NM_Client && IsValid(NPC.Get()) && NPC->HasAuthority() && !NPC->IsActorBeingDestroyed()
			&& GetOwner() == NPC.Get() && IsValid(AI.Get()) && Controller.Get() == AI.Get() && !AI->IsActorBeingDestroyed()
			&& AI->GetPawn() == NPC.Get() && NPC->GetController() == AI.Get() && AI->GetPathFollowingComponent()
			&& (!AI->GetBrainComponent() || !AI->GetBrainComponent()->IsRunning())
			&& IsValid(Scoring.Get()) && ScoringComponent.Get() == Scoring.Get() && Scoring->IsRegistered() && !Scoring->IsBeingDestroyed()
			&& Scoring->IsBatchedNPCDecisionRegistered() && Paths.IsValid()
			&& IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && AbilitySystem.Get() == ASC.Get()
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

bool UProject_JNPCActionComponent::HasForeignMovement() const
{
	const auto* AI = Controller.Get();
	const auto* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following && Following->GetStatus() != EPathFollowingStatus::Idle
		&& (!MoveId.IsValid() || Following->GetCurrentRequestId() != MoveId);
}

bool UProject_JNPCActionComponent::PrepareForAttack(AActor* Target)
{
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
	if (const auto* Pawn = Cast<APawn>(IntentTarget.Get())) { return Pawn->GetNavAgentLocation(); }
	return IntentTarget.IsValid() ? IntentTarget->GetActorLocation() : FVector::ZeroVector;
}

void UProject_JNPCActionComponent::OnScored(AActor* Target, double Score)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	const uint64 Action = ActionRevision, BeforeIntent = IntentRevision;
	if (!IsContextValid()) { if (ActionRevision == Action && IntentRevision == BeforeIntent) { StopActions(); } return; }
	if (ActionRevision != Action || IntentRevision != BeforeIntent) { return; }
	if (Target != IntentTarget.Get())
	{
		ClearIntent();
		// A non-cancellable old attack must not silently read a new target from this component.
		if (!bEnabled || bOwnsAttack || ActionRevision != Action || IntentRevision != BeforeIntent + 1) { return; }
		IntentTarget = Target;
	}
	LastDecision = FPlatformTime::Seconds();
	const uint64 CurrentIntent = IntentRevision;
	if (!IsTargetValid() && ActionRevision == Action && IntentRevision == CurrentIntent) { ClearIntent(); }
}

void UProject_JNPCActionComponent::CancelPathAndMove()
{
	const uint64 Token = PathToken; PathToken = 0;
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
	if (!IsTargetValid())
	{
		if (!IsCurrent()) { return; }
		UE_CLOG(IntentTarget.IsValid() && CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
			TEXT("Intent invalid Owner=%s Target=%s Distance=%.1f Range=%.1f Age=%.2f"), *GetNameSafe(GetOwner()),
			*GetNameSafe(IntentTarget.Get()), IntentTarget.IsValid() ? FVector::Dist(GetOwner()->GetActorLocation(), IntentTarget->GetActorLocation()) : 0,
			ScoringComponent->Range, FPlatformTime::Seconds() - LastDecision);
		ClearIntent(); return;
	}
	if (!IsCurrent()) { return; }
	const double Now = FPlatformTime::Seconds();
	if (bOwnsAttack) { State = EProjectJNPCActionState::Attacking; return; }
	const FVector Delta = IntentTarget->GetActorLocation() - GetOwner()->GetActorLocation();
	if (Delta.SizeSquared() <= FMath::Square(AttackRange) && Controller->LineOfSightTo(IntentTarget.Get()))
	{
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
	if (Goal.ContainsNaN() || Goal.GetAbsMax() > 1.e9) { ClearIntent(); return; }
	if (MoveId.IsValid())
	{
		const auto* Following = Controller->GetPathFollowingComponent();
		const auto Path = Following->GetPath();
		if (Following->GetStatus() == EPathFollowingStatus::Idle || !Path.IsValid() || !Path->IsValid() || !Path->IsUpToDate())
		{
			// A pending replacement remains useful even if the old path has become invalid.
			CancelMove();
			if (!IsCurrent()) { return; }
			if (bEnabled) { State = PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff; }
		}
	}
	if (!IsTargetValid()) { if (IsCurrent()) { ClearIntent(); } return; }
	if (!IsCurrent()) { return; }
	if (PathToken || Now < NextPathTime) { return; }
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
	if (!IsContextValid() || HasForeignMovement()) { if (IsCurrent()) { StopActions(); } return; }
	if (!IsCurrent()) { return; }
	if (!IsTargetValid()) { if (IsCurrent()) { ClearIntent(); } return; }
	if (!IsCurrent()) { return; }
	NextPathTime = FPlatformTime::Seconds() + RetryInterval;
	const auto* Pawn = CastChecked<APawn>(GetOwner());
	const auto* Spec = AttackHandle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(AttackHandle) : nullptr;
	if (Spec && Spec->IsActive()) { State = EProjectJNPCActionState::InRange; return; }
	if (Completion.Status != EProjectJNPCPathStatus::Success || !Completion.Path.IsValid()
		|| !Completion.Path->IsValid() || !Completion.Path->IsUpToDate() || Completion.Path->IsPartial()
		|| GetTargetGoal().ContainsNaN() || Pawn->GetNavAgentLocation().ContainsNaN()
		|| FVector::DistSquared(GetTargetGoal(), Completion.Goal) > FMath::Square(RepathDistance)
		|| FVector::DistSquared(Pawn->GetNavAgentLocation(), Completion.Start) > FMath::Square(RepathDistance))
	{
		UE_CLOG(CVarNPCActionDebug.GetValueOnGameThread() != 0, LogProjectJNPCAction, Log,
			TEXT("Path discarded Owner=%s Status=%d StartDrift=%.1f GoalDrift=%.1f"), *GetNameSafe(GetOwner()), int32(Completion.Status),
			FVector::Dist(Pawn->GetNavAgentLocation(), Completion.Start), FVector::Dist(GetTargetGoal(), Completion.Goal));
		if (Completion.Status != EProjectJNPCPathStatus::Success) { ++Stats.PathFailed; BackoffPath(); }
		else { ++Stats.PathStale; }
		State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath : EProjectJNPCActionState::Backoff; return;
	}
	FAIMoveRequest Move;
	Move.SetGoalLocation(Completion.Goal);
	Move.SetAcceptanceRadius(25.0f).SetReachTestIncludesAgentRadius(false).SetReachTestIncludesGoalRadius(false);
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

void UProject_JNPCActionComponent::StopActions()
{
	check(IsInGameThread());
	if (bStopping) { return; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	TGuardValue<bool> Guard(bStopping, true);
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
	ClearIntent();
	if (auto* AI = Controller.Get())
	{
		if (auto* Following = AI->GetPathFollowingComponent()) { Following->OnRequestFinished.Remove(MoveHandle); }
	}
	if (auto* ASC = AbilitySystem.Get()) { ASC->OnAbilityEnded.Remove(AbilityEndedHandle); }
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	bOwnsAttack = false;
	ScoringComponent.Reset(); Paths.Reset(); Controller.Reset(); AbilitySystem.Reset(); AttackHandle = {};
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
	const FProjectJNPCActionSuspension Before {ScoringComponent, AttackHandle, ActionRevision + 1, OwnershipToken};
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
void UProject_JNPCActionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true; StopActions(); Super::EndPlay(Reason);
}
void UProject_JNPCActionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true; StopActions(); Super::OnComponentDestroyed(bDestroyingHierarchy);
}
