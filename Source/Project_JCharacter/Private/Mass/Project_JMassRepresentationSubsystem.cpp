#include "Mass/Project_JMassRepresentationSubsystem.h"
#include "Mass/Project_JMassMovementProcessor.h"
#include "MassEntityManager.h"
#include "MassEntityManagerStorage.h"
#include "MassProcessingContext.h"
#include "MassExecutor.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "HAL/IConsoleManager.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavMesh/NavMeshPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
TAutoConsoleVariable<int32> ParallelMode(TEXT("ProjectJ.Mass.Parallel"), -1,
	TEXT("-1: use parallel crowd chunks for 1024+ moving agents; 0: serial; 1: force parallel. Includes snapshot/join cost in profiling."));
TAutoConsoleVariable<int32> CrowdSpacing(TEXT("ProjectJ.Mass.CrowdSpacing"), 1,
	TEXT("Bounded neighbor snapshot and corridor-preserving spacing for opt-in Mass NPCs. Dense/blocked routes return to Character."));
}

struct FProjectJMassRepresentationEntry
{
	TWeakObjectPtr<AProject_JNPCCharacter> NPC;
	TWeakObjectPtr<UProject_JNPCActionComponent> Action;
	TWeakObjectPtr<UProject_JTargetScoringComponent> Scoring;
	FGameplayAbilitySpecHandle Attack;
	FMassEntityHandle Entity;
	FNavPathSharedPtr Path; // GT only; worker receives copied points, never this shared pointer.
	uint64 Id = 0, Generation = 1;
	bool bMass = false, bActionWasEnabled = false;
	bool bMovementTick = false, bMeshTick = false, bHidden = false, bCollision = false, bDamage = false;
	FVector CapsuleOffset = FVector::ZeroVector;
};

bool UProject_JMassRepresentationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }

void UProject_JMassRepresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	check(IsInGameThread()); static uint64 NextWorldEpoch = 0; WorldEpoch = ++NextWorldEpoch;
	Super::Initialize(Collection); bAccepting = true;
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
}

FProjectJMassAgentToken UProject_JMassRepresentationSubsystem::RegisterNPC(AProject_JNPCCharacter* NPC)
{
	check(IsInGameThread());
	if (!bAccepting || bStepping || !IsValid(NPC) || NPC->GetWorld() != GetWorld() || GetWorld()->bIsTearingDown
		|| GetWorld()->GetNetMode() == NM_Client || !NPC->HasAuthority() || Entries.Num() >= MaxAgents
		|| Entries.ContainsByPredicate([NPC](const auto& E) { return E->NPC.Get() == NPC; })) { return {}; }
	if (!Entities)
	{
		Entities = MakeShared<FMassEntityManager>(this);
		FMassEntityManagerStorageInitParams Params;
#if WITH_MASS_CONCURRENT_RESERVE
		Params.Emplace<FMassEntityManager_InitParams_Concurrent>(FMassEntityManager_InitParams_Concurrent{4096, 1024});
#else
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Params.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
		Entities->Initialize(Params);
		const UScriptStruct* Fragments[] = {FProjectJMassMovementFragment::StaticStruct()};
		Archetype = Entities->CreateArchetype(Fragments);
		Processor = NewObject<UProject_JMassMovementProcessor>(this);
		Processor->CallInitialize(this, Entities.ToSharedRef());
	}
	auto Entry = MakeShared<FProjectJMassRepresentationEntry>();
	Entry->NPC = NPC; Entry->Id = ++NextId; Entry->Entity = Entities->CreateEntity(Archetype);
	auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(Entry->Entity);
	F.StableId = Entry->Id; F.Position = NPC->GetNavAgentLocation();
	Entries.Add(Entry); Stats.Registered = Entries.Num();
	return {Entry->Id, Entry->Generation, WorldEpoch};
}

FProjectJMassAgentToken UProject_JMassRepresentationSubsystem::GetToken(uint64 Id) const
{
	for (const auto& E : Entries) { if (E->Id == Id) { return {Id, E->Generation, WorldEpoch}; } }
	return {};
}

bool UProject_JMassRepresentationSubsystem::SetRoute(FProjectJMassAgentToken Token, const FNavPathSharedPtr& Path)
{
	check(IsInGameThread());
	if (!bAccepting || bStepping) { return false; }
	if (Token.WorldEpoch != WorldEpoch) { ++Stats.RejectedStale; return false; }
	for (const auto& E : Entries)
	{
		if (E->Id != Token.Id) { continue; }
		if (E->Generation != Token.Generation) { ++Stats.RejectedStale; return false; }
		if (!Path || !Path->IsValid() || !Path->IsUpToDate() || Path->IsPartial()
			|| Path->GetPathPoints().Num() < 2 || Path->GetPathPoints().Num() > FProjectJMassMovementFragment::MaxRoutePoints || !E->NPC.IsValid()) { return false; }
		// Off-mesh links require Character traversal, and cannot be consumed as a straight segment.
		for (const auto& Point : Path->GetPathPoints())
		{ if (Point.Location.ContainsNaN() || Point.CustomNavLinkId.IsValid() || FNavMeshNodeFlags(Point.Flags).IsNavLink()) { return false; } }
		if (FVector::DistSquared(E->NPC->GetNavAgentLocation(), Path->GetPathPoints()[0].Location) > FMath::Square(100.0)) { return false; }
		auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E->Entity);
		F.RouteCount = 0; for (const auto& Point : Path->GetPathPoints()) { F.Route[F.RouteCount++] = Point.Location; }
		F.NextPoint = 1; F.Position = E->NPC->GetNavAgentLocation(); F.bRouteValid = true; E->Path = Path;
		F.bNeedsCharacterTraversal = false; F.BlockedSeconds = 0;
		return true;
	}
	++Stats.RejectedStale; return false;
}

bool UProject_JMassRepresentationSubsystem::IsMassOwned(uint64 Id) const
{ for (const auto& E : Entries) { if (E->Id == Id) { return E->bMass; } } return false; }

bool UProject_JMassRepresentationSubsystem::CanDemote(const FProjectJMassRepresentationEntry& E) const
{
	const auto* NPC = E.NPC.Get();
	if (!NPC || !E.Path || !E.Path->IsValid() || !E.Path->IsUpToDate() || NPC->IsActorBeingDestroyed()) { return false; }
	if (Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E.Entity).bNeedsCharacterTraversal) { return false; }
	const auto* ASC = NPC->GetAbilitySystemComponent();
	const auto* Movement = NPC->GetCharacterMovement();
	if (!FMath::IsFinite(Movement->MaxWalkSpeed) || Movement->MaxWalkSpeed <= 0 || Movement->MaxWalkSpeed > 1000 || Movement->HasRootMotionSources()) { return false; }
	if (NPC->GetCapsuleComponent()->GetScaledCapsuleRadius() > 90) { return false; }
	const auto* BudgetMesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh());
	const auto* Anim = NPC->GetMesh()->GetAnimInstance();
	if (NPC->GetMesh()->IsSimulatingPhysics() || (BudgetMesh && BudgetMesh->IsCombatCritical())
		|| (Anim && (Anim->Montage_IsPlaying(nullptr) || Anim->RootMotionMode == ERootMotionMode::RootMotionFromEverything))) { return false; }
	if (!ASC || NPC->GetAttributeSet()->GetHealth() <= 0 || ASC->GetActiveEffects(FGameplayEffectQuery()).Num() > 0) { return false; }
	if (!E.bMass && FVector::DistSquared(NPC->GetNavAgentLocation(), E.Path->GetPathPoints()[0].Location) > FMath::Square(100.0)) { return false; }
	if (E.bMass && NPC->GetCharacterMovement()->IsComponentTickEnabled()) { return false; }
	for (const auto& Spec : ASC->GetActivatableAbilities()) { if (Spec.IsActive()) { return false; } }
	if (const auto* Action = NPC->FindComponentByClass<UProject_JNPCActionComponent>())
	{
		if (Action->GetActionState() != EProjectJNPCActionState::Disabled) { return !E.bMass && Action->CanSuspendMovement(); }
	}
	const auto* AI = Cast<AAIController>(NPC->GetController());
	return !AI || !AI->GetPathFollowingComponent() || AI->GetPathFollowingComponent()->GetStatus() == EPathFollowingStatus::Idle;
}

void UProject_JMassRepresentationSubsystem::Demote(FProjectJMassRepresentationEntry& E)
{
	auto* NPC = E.NPC.Get(); auto* Movement = NPC->GetCharacterMovement();
	E.Action = NPC->FindComponentByClass<UProject_JNPCActionComponent>();
	E.bActionWasEnabled = E.Action.IsValid() && E.Action->GetActionState() != EProjectJNPCActionState::Disabled;
	if (E.bActionWasEnabled)
	{
		E.Scoring = E.Action->GetScoringSource(); E.Attack = E.Action->GetAttackAbilityHandle();
		E.Action->StopActions(); // Cancels only owned move/intent/path; physical engine slots stay reserved.
	}
	// StopActions can invoke callbacks. Recheck ownership and combat pins before taking movement.
	if (!bAccepting || !E.NPC.IsValid() || NPC->IsActorBeingDestroyed() || !CanDemote(E)
		|| (E.Action.IsValid() && E.Action->GetActionState() != EProjectJNPCActionState::Disabled)) { return; }
	E.bMovementTick = Movement->IsComponentTickEnabled(); E.bHidden = NPC->IsHidden();
	const auto* BudgetMesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(NPC->GetMesh());
	E.bMeshTick = BudgetMesh ? BudgetMesh->GetRequestedTickEnabled() : NPC->GetMesh()->IsComponentTickEnabled();
	E.bCollision = NPC->GetActorEnableCollision(); E.bDamage = NPC->CanBeDamaged();
	E.CapsuleOffset = NPC->GetActorLocation() - NPC->GetNavAgentLocation();
	Movement->StopMovementImmediately(); Movement->SetComponentTickEnabled(false);
	NPC->GetMesh()->SetComponentTickEnabled(false); // ABA observes the requested state and releases its ownership.
	NPC->SetActorEnableCollision(false); NPC->SetCanBeDamaged(false); NPC->SetActorHiddenInGame(true);
	auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E.Entity);
	F.Position = NPC->GetNavAgentLocation(); F.Speed = Movement->MaxWalkSpeed;
	F.Radius = NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
	F.HalfHeight = NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	F.Generation = ++E.Generation; F.bMassOwnsMovement = true; E.bMass = true;
	++Stats.Demoted; ++Stats.LastTransitions;
}

void UProject_JMassRepresentationSubsystem::Promote(FProjectJMassRepresentationEntry& E, bool bResumeActions)
{
	if (!E.bMass) { return; }
	auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E.Entity);
	F.bMassOwnsMovement = false; F.Generation = ++E.Generation; E.bMass = false;
	if (auto* NPC = E.NPC.Get(); NPC && !NPC->IsActorBeingDestroyed())
	{
		NPC->GetCharacterMovement()->SetComponentTickEnabled(E.bMovementTick);
		NPC->GetMesh()->SetComponentTickEnabled(E.bMeshTick);
		NPC->SetActorEnableCollision(E.bCollision); NPC->SetCanBeDamaged(E.bDamage); NPC->SetActorHiddenInGame(E.bHidden);
		NPC->ForceNetUpdate();
		if (bResumeActions && E.bActionWasEnabled && E.Action.IsValid() && E.Scoring.IsValid()
			&& E.Action->GetActionState() == EProjectJNPCActionState::Disabled)
		{ E.Action->StartActions(E.Scoring.Get(), E.Attack); }
	}
	++Stats.Promoted; ++Stats.LastTransitions;
}

bool UProject_JMassRepresentationSubsystem::UnregisterNPC(FProjectJMassAgentToken Token)
{
	check(IsInGameThread());
	if (bStepping) { return false; }
	if (Token.WorldEpoch != WorldEpoch) { ++Stats.RejectedStale; return false; }
	for (int32 I = 0; I < Entries.Num(); ++I)
	{
		const auto E = Entries[I]; if (E->Id != Token.Id) { continue; }
		if (E->Generation != Token.Generation) { ++Stats.RejectedStale; return false; }
		bStepping = true; ON_SCOPE_EXIT { bStepping = false; if (!bAccepting) { Stop(); } };
		const bool bWasMass = E->bMass;
		Promote(*E, bAccepting); Entities->DestroyEntity(E->Entity);
		if (bWasMass) { Stats.MassOwned = FMath::Max(0, Stats.MassOwned - 1); }
		Entries.RemoveAt(I); Stats.Registered = Entries.Num(); return true;
	}
	++Stats.RejectedStale; return false;
}

void UProject_JMassRepresentationSubsystem::Tick(float DeltaSeconds)
{
	check(IsInGameThread());
	if (!IsTickable() || bStepping) { return; }
	bStepping = true;
	ON_SCOPE_EXIT { bStepping = false; if (!bAccepting) { Stop(); } };
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_MassRepresentation_GT);
	const double Start = FPlatformTime::Seconds(); Stats.LastTransitions = 0;
	TArray<FVector> Observers;
#if WITH_DEV_AUTOMATION_TESTS
	if (bTestObservers) { Observers = TestObservers; }
	else
#endif
	{
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{ FVector Location; FRotator Rotation; if (auto* PC = It->Get()) { PC->GetPlayerViewPoint(Location, Rotation); Observers.Add(Location); } }
	}
	const int32 Count = Entries.Num();
	int32 MovingCount = 0;
	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		const auto E = Entries[(Cursor + Offset) % Count]; auto* NPC = E->NPC.Get();
		if (!NPC || NPC->IsActorBeingDestroyed()) { continue; }
		auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E->Entity);
		if (!E->bMass)
		{
			F.Position = NPC->GetNavAgentLocation(); F.Radius = NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
			F.HalfHeight = NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
		F.bRouteValid = E->Path && E->Path->IsValid() && E->Path->IsUpToDate();
		double Distance = Observers.IsEmpty() ? 0 : TNumericLimits<double>::Max();
		for (const auto& P : Observers) { Distance = FMath::Min(Distance, FVector::Distance(P, NPC->GetActorLocation())); }
		const bool bWantCharacter = Distance < PromoteDistance || !F.bRouteValid || !CanDemote(*E);
		const bool bWantMass = !E->bMass && Distance > DemoteDistance && !bWantCharacter;
		if (E->bMass && bWantCharacter) { F.bRouteValid = false; } // Freeze while waiting for a transition slot.
		if ((E->bMass && bWantCharacter) || bWantMass)
		{
			if (Stats.LastTransitions >= MaxTransitionsPerTick) { ++Stats.Deferred; continue; }
			if (E->bMass) { Promote(*E, true); } else { Demote(*E); }
			if (!bAccepting) { return; }
		}
		if (E->bMass && F.bRouteValid && !F.bNeedsCharacterTraversal && F.NextPoint < F.RouteCount) { ++MovingCount; }
	}
	Cursor = Count ? (Cursor + MaxTransitionsPerTick) % Count : 0;
	Processor->bUseCrowdSpacing = CrowdSpacing.GetValueOnGameThread() != 0;
	const int32 Mode = ParallelMode.GetValueOnGameThread();
	Processor->bParallel = Mode > 0 || (Mode < 0 && Processor->bUseCrowdSpacing && MovingCount >= 1024);
	UE::Mass::FProcessingContext Context(Entities, DeltaSeconds);
	UE::Mass::Executor::Run(*Processor, Context); // Joins chunk tasks before reading results or changing ownership.
	Stats.MassOwned = 0;
	for (int32 I = Entries.Num() - 1; I >= 0; --I)
	{
		const auto E = Entries[I]; auto* NPC = E->NPC.Get();
		if (!NPC || NPC->IsActorBeingDestroyed()) { Entities->DestroyEntity(E->Entity); Entries.RemoveAt(I); continue; }
		if (E->bMass)
		{
			const auto& F = Entities->GetFragmentDataChecked<FProjectJMassMovementFragment>(E->Entity);
			NPC->SetActorLocation(F.Position + E->CapsuleOffset, false, nullptr, ETeleportType::TeleportPhysics);
			if (!bAccepting) { return; }
			++Stats.MassOwned;
		}
	}
	Stats.Registered = Entries.Num(); Stats.LastStepMilliseconds = (FPlatformTime::Seconds() - Start) * 1000;
}

bool UProject_JMassRepresentationSubsystem::IsTickable() const
{ return !IsTemplate() && bAccepting && !Entries.IsEmpty() && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_Client; }
TStatId UProject_JMassRepresentationSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJMassRepresentation, STATGROUP_Tickables); }
void UProject_JMassRepresentationSubsystem::Stop()
{
	bAccepting = false; if (bStepping) { return; }
	TGuardValue<bool> Guard(bStepping, true);
	for (const auto& E : Entries) { Promote(*E, false); Entities->DestroyEntity(E->Entity); }
	Entries.Reset(); Stats.Registered = Stats.MassOwned = 0;
}
void UProject_JMassRepresentationSubsystem::OnTearDown(UWorld* World) { if (World == GetWorld()) { Stop(); } }
void UProject_JMassRepresentationSubsystem::OnWorldEndPlay(UWorld& World) { Stop(); Super::OnWorldEndPlay(World); }
void UProject_JMassRepresentationSubsystem::Deinitialize()
{
	Stop(); FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	Processor = nullptr; Entities.Reset(); Super::Deinitialize();
}
