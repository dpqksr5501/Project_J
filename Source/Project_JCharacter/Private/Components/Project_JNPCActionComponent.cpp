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
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPCAction, Log, All);

UProject_JNPCActionComponent::UProject_JNPCActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
}

bool UProject_JNPCActionComponent::IsAttackSupported() const
{
	if (!AttackHandle.IsValid()) { return true; }
	const auto* ASC = AbilitySystem.Get();
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(AttackHandle) : nullptr;
	if (!Spec || !Spec->Ability || Spec->PendingRemove || Spec->RemoveAfterActivation) { return false; }
	const auto Policy = Spec->Ability->GetNetExecutionPolicy();
	return Spec->Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor
		&& (Policy == EGameplayAbilityNetExecutionPolicy::ServerOnly || Policy == EGameplayAbilityNetExecutionPolicy::ServerInitiated);
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
	for (const auto* Peer : Peers) { if (Peer != this && Peer->bEnabled) { return false; } }
	AbilitySystem = NPC->GetAbilitySystemComponent(); AttackHandle = AttackAbility;
	if (!AbilitySystem.IsValid() || AbilitySystem->GetAvatarActor() != NPC || !IsAttackSupported())
	{
		AbilitySystem.Reset(); AttackHandle = {}; return false;
	}
	Paths = GetWorld()->GetSubsystem<UProject_JNPCPathSubsystem>();
	if (!Paths.IsValid()) { AbilitySystem.Reset(); AttackHandle = {}; return false; }
	Controller = AI; ScoringComponent = Scoring;
	Scoring->OnQueryCompleted.AddUniqueDynamic(this, &ThisClass::OnScored);
	ContextHandle = Scoring->OnContextInvalidated.AddUObject(this, &ThisClass::ClearIntent);
	MoveHandle = AI->GetPathFollowingComponent()->OnRequestFinished.AddUObject(this, &ThisClass::OnMoveFinished);
	AbilityEndedHandle = AbilitySystem->OnAbilityEnded.AddUObject(this, &ThisClass::OnAbilityEnded);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
	bEnabled = true; State = EProjectJNPCActionState::Idle;
	NextPathTime = NextAttackTime = 0;
	SetComponentTickEnabled(true);
	return true;
}

bool UProject_JNPCActionComponent::IsContextValid() const
{
	const auto* NPC = Cast<AProject_JNPCCharacter>(GetOwner());
	const auto* AI = Controller.Get();
	return bEnabled && !bEndingPlay && !IsBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown
		&& GetWorld()->GetNetMode() != NM_Client && IsValid(NPC) && NPC->HasAuthority() && !NPC->IsActorBeingDestroyed()
		&& !IProject_JCombatInterface::Execute_IsDead(const_cast<AProject_JNPCCharacter*>(NPC))
		&& AI && !AI->IsActorBeingDestroyed() && AI->GetPawn() == NPC
		&& NPC->GetController() == AI && AI->GetPathFollowingComponent()
		&& (!AI->GetBrainComponent() || !AI->GetBrainComponent()->IsRunning())
		&& ScoringComponent.IsValid() && ScoringComponent->IsRegistered() && !ScoringComponent->IsBeingDestroyed()
		&& ScoringComponent->IsBatchedNPCDecisionRegistered() && Paths.IsValid()
		&& AbilitySystem.IsValid() && NPC->GetAbilitySystemComponent() == AbilitySystem.Get()
		&& AbilitySystem->GetAvatarActor() == NPC && IsAttackSupported();
}

bool UProject_JNPCActionComponent::IsTargetValid() const
{
	auto* Target = IntentTarget.Get();
	const auto* Scheduler = GetWorld() ? GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>() : nullptr;
	return IsContextValid() && IsValid(Target) && !Target->GetActorLocation().ContainsNaN()
		&& !GetOwner()->GetActorLocation().ContainsNaN() && Scheduler
		&& Scheduler->CanActOnTarget(ScoringComponent.Get(), Target)
		&& FMath::IsFinite(ScoringComponent->Range) && ScoringComponent->Range > 0
		&& FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(ScoringComponent->Range)
		&& FPlatformTime::Seconds() - LastDecision <= DecisionLifetime;
}

bool UProject_JNPCActionComponent::HasForeignMovement() const
{
	const auto* AI = Controller.Get();
	const auto* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following && Following->GetStatus() != EPathFollowingStatus::Idle
		&& (!MoveId.IsValid() || Following->GetCurrentRequestId() != MoveId);
}

FVector UProject_JNPCActionComponent::GetTargetGoal() const
{
	if (const auto* Pawn = Cast<APawn>(IntentTarget.Get())) { return Pawn->GetNavAgentLocation(); }
	return IntentTarget.IsValid() ? IntentTarget->GetActorLocation() : FVector::ZeroVector;
}

void UProject_JNPCActionComponent::OnScored(AActor* Target, double Score)
{
	check(IsInGameThread());
	if (!IsContextValid()) { StopActions(); return; }
	if (Target != IntentTarget.Get())
	{
		ClearIntent();
		// A non-cancellable old attack must not silently read a new target from this component.
		if (!bEnabled || bOwnsAttack) { return; }
		IntentTarget = Target;
	}
	LastDecision = FPlatformTime::Seconds();
	if (!IsTargetValid()) { ClearIntent(); }
}

void UProject_JNPCActionComponent::CancelPathAndMove()
{
	const uint64 Token = PathToken; PathToken = 0;
	if (auto* Service = Paths.Get()) { Service->Cancel(Token); }
	const FAIRequestID Previous = MoveId; MoveId = FAIRequestID::InvalidRequest;
	if (auto* AI = Controller.Get())
	{
		auto* Following = AI->GetPathFollowingComponent();
		if (Following && Previous.IsValid() && Following->GetCurrentRequestId() == Previous)
		{
			Following->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, Previous);
		}
	}
}

void UProject_JNPCActionComponent::CancelOwnedAttack()
{
	if (!bOwnsAttack) { return; }
	if (auto* ASC = AbilitySystem.Get()) { ASC->CancelAbilityHandle(AttackHandle); }
	// Non-cancellable ability phases remain GAS-owned; do not force EndAbility or cancel unrelated grants.
}

void UProject_JNPCActionComponent::ClearIntent()
{
	check(IsInGameThread());
	++IntentRevision; IntentTarget.Reset();
	CancelPathAndMove(); CancelOwnedAttack();
	State = bEnabled ? EProjectJNPCActionState::Idle : EProjectJNPCActionState::Disabled;
}

void UProject_JNPCActionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCAction_Update);
	if (!IsContextValid() || HasForeignMovement()) { StopActions(); return; }
	if (!IsTargetValid()) { ClearIntent(); return; }
	const double Now = FPlatformTime::Seconds();
	if (bOwnsAttack) { State = EProjectJNPCActionState::Attacking; return; }
	const FVector Delta = IntentTarget->GetActorLocation() - GetOwner()->GetActorLocation();
	if (Delta.SizeSquared() <= FMath::Square(AttackRange) && Controller->LineOfSightTo(IntentTarget.Get()))
	{
		CancelPathAndMove();
		if (!IsTargetValid()) { ClearIntent(); return; }
		State = EProjectJNPCActionState::InRange;
		if (!AttackHandle.IsValid() || Now < NextAttackTime) { return; }
		auto* Spec = AbilitySystem->FindAbilitySpecFromHandle(AttackHandle);
		if (!Spec || Spec->IsActive()) { return; }
		NextAttackTime = Now + RetryInterval;
		// Set ownership before activation: instant abilities can end synchronously in TryActivateAbility.
		const uint64 Revision = IntentRevision;
		bOwnsAttack = true;
		const bool Activated = AbilitySystem->TryActivateAbility(AttackHandle, false);
		if (!Activated) { bOwnsAttack = false; }
		if (!bEnabled || IntentRevision != Revision) { CancelOwnedAttack(); return; }
		if (bOwnsAttack) { State = EProjectJNPCActionState::Attacking; }
		return;
	}
	// A different system's active attack is not ours to cancel or move through.
	if (AttackHandle.IsValid())
	{
		const auto* Spec = AbilitySystem->FindAbilitySpecFromHandle(AttackHandle);
		if (Spec && Spec->IsActive()) { CancelPathAndMove(); State = EProjectJNPCActionState::InRange; return; }
	}
	const FVector Goal = GetTargetGoal();
	if (Goal.ContainsNaN() || Goal.GetAbsMax() > 1.e9) { ClearIntent(); return; }
	if ((PathToken || MoveId.IsValid()) && FVector::DistSquared(Goal, RequestedGoal) > FMath::Square(RepathDistance))
	{
		++IntentRevision; CancelPathAndMove();
	}
	if (MoveId.IsValid())
	{
		const auto* Following = Controller->GetPathFollowingComponent();
		const auto Path = Following->GetPath();
		if (Following->GetStatus() == EPathFollowingStatus::Idle || !Path.IsValid() || !Path->IsValid() || !Path->IsUpToDate())
		{
			++IntentRevision; CancelPathAndMove(); NextPathTime = Now + RetryInterval;
			if (bEnabled) { State = EProjectJNPCActionState::Backoff; }
		}
	}
	if (!IsTargetValid()) { ClearIntent(); return; }
	if (PathToken || MoveId.IsValid() || Now < NextPathTime) { return; }
	RequestedGoal = Goal; NextPathTime = Now + RetryInterval;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	PathToken = Paths->Submit(this, CastChecked<APawn>(GetOwner()), Goal, IntentRevision,
		[WeakThis](const FProjectJNPCPathCompletion& Completion)
		{
			if (auto* Self = WeakThis.Get()) { Self->OnPathReady(Completion); }
		});
	State = PathToken ? EProjectJNPCActionState::AwaitingPath : EProjectJNPCActionState::Backoff;
}

void UProject_JNPCActionComponent::OnPathReady(const FProjectJNPCPathCompletion& Completion)
{
	check(IsInGameThread());
	if (!bEnabled || PathToken != Completion.Token || IntentRevision != Completion.IntentRevision) { return; }
	PathToken = 0;
	if (!IsContextValid() || HasForeignMovement()) { StopActions(); return; }
	if (!IsTargetValid()) { ClearIntent(); return; }
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
		State = EProjectJNPCActionState::Backoff; return;
	}
	FAIMoveRequest Move;
	Move.SetGoalLocation(Completion.Goal);
	Move.SetAcceptanceRadius(25.0f).SetReachTestIncludesAgentRadius(false).SetReachTestIncludesGoalRadius(false);
	// RequestMove consumes our completed path. MoveTo would synchronously find another one.
	const uint64 Revision = IntentRevision;
	const TWeakObjectPtr<AAIController> RequestController = Controller;
	const FAIRequestID StartedMove = Controller->RequestMove(Move, Completion.Path);
	if (!bEnabled || IntentRevision != Revision)
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
	State = MoveId.IsValid() ? EProjectJNPCActionState::FollowingPath : EProjectJNPCActionState::Backoff;
}

void UProject_JNPCActionComponent::OnMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result)
{
	if (!bEnabled || !MoveId.IsValid() || MoveId != RequestId) { return; }
	MoveId = FAIRequestID::InvalidRequest;
	NextPathTime = FPlatformTime::Seconds() + RetryInterval;
	State = EProjectJNPCActionState::Backoff;
}
void UProject_JNPCActionComponent::OnAbilityEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle == AttackHandle) { bOwnsAttack = false; }
}

void UProject_JNPCActionComponent::StopActions()
{
	check(IsInGameThread());
	if (bStopping) { return; }
	TGuardValue<bool> Guard(bStopping, true);
	bEnabled = false; SetComponentTickEnabled(false);
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
void UProject_JNPCActionComponent::OnTearDown(UWorld* World) { if (World == GetWorld()) { StopActions(); } }
void UProject_JNPCActionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true; StopActions(); Super::EndPlay(Reason);
}
void UProject_JNPCActionComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true; StopActions(); Super::OnComponentDestroyed(bDestroyingHierarchy);
}
