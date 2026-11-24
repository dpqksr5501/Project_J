#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "GameFramework/Actor.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JMeleeSweep.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimMontage.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJAuthoredMelee, Log, All);

namespace
{
bool CanProduceMeleeContacts(const AActor* Owner)
{
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() &&
		(Owner->HasAuthority() || Owner->GetLocalRole() == ROLE_AutonomousProxy);
}

void LogAuthorityMeleeSweep(const AActor& Owner, int32 Index, float Radius, const FVector& Start, const FVector& End, const TArray<FHitResult>& Hits)
{
	if (!Owner.HasAuthority() || !UE_LOG_ACTIVE(LogProjectJAuthoredMelee, VeryVerbose)) return;
	const FString Targets = FString::JoinBy(Hits, TEXT(","), [](const FHitResult& Hit) { return GetNameSafe(Hit.GetActor()); });
	UE_LOG(LogProjectJAuthoredMelee, VeryVerbose, TEXT("MeleeSweep frame=%llu source=%s segment=%d radius=%.3f start=%s end=%s hits=%s"),
		GFrameCounter, *Owner.GetName(), Index, Radius, *Start.ToCompactString(), *End.ToCompactString(), *Targets);
}

void LogAuthorityMeleeEvent(const AActor& Owner, const AActor& Target)
{
	if (Owner.HasAuthority())
	{
		UE_LOG(LogProjectJAuthoredMelee, VeryVerbose, TEXT("MeleeEvent frame=%llu source=%s target=%s"),
			GFrameCounter, *Owner.GetName(), *Target.GetName());
	}
}
}

UProject_JAnimNotifyState_MeleeHit::UProject_JAnimNotifyState_MeleeHit()
{
}

void UProject_JAnimNotifyState_MeleeHit::BeginDestroy()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
	Super::BeginDestroy();
}

void UProject_JAnimNotifyState_MeleeHit::PruneCollectedMeshes()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeWeakMeshPrune);
	check(IsInGameThread());
	for (auto It = TraceStates.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
}

void UProject_JAnimNotifyState_MeleeHit::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeBegin);
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!IsValid(MeshComp)) { return; }
	// Other clients already receive attack presentation through its own notify/replication path.
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!CanProduceMeleeContacts(OwnerActor)) { return; }
	if (SocketName.IsNone() || !MeshComp->DoesSocketExist(SocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("Melee Hit Trace requires Leader mesh socket '%s' on %s; hit window will not open."),
			*SocketName.ToString(), *GetNameSafe(MeshComp));
		return;
	}
	if (!UProject_JGameplayAbility_Melee::CanDispatchAttackNotify(*MeshComp, EventReference)) { return; }
	// The asset is shared between meshes. GC retires dead entries without scanning
	// all visible actors on every Begin.
	if (!PostGCHandle.IsValid()) PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ThisClass::PruneCollectedMeshes);
	if (auto* ExistingStates = TraceStates.Find(MeshComp))
	{
		if (ExistingStates->Contains(EventReference.GetNotifyInstanceID())) { return; }
		for (auto It = ExistingStates->CreateIterator(); It; ++It)
		{
			if (It.Value().WindowToken && (!It.Value().Validation.IsValid() || !It.Value().Validation->IsHitWindowCurrent(It.Value().WindowToken))) { It.RemoveCurrent(); }
		}
	}
	auto* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>();

	const auto* Attack = HitValidation ? HitValidation->GetActiveAttackDefinition() : nullptr;
	const bool bAuthored = Attack && Attack->MeleeTrajectory.bEnabled;
	const FProject_JMeleeTrajectory* Path = nullptr;
	if (bAuthored)
	{
		const auto* Event = EventReference.GetNotify();
		Path = Event ? Attack->FindMeleeTrajectory(*Event) : nullptr;
		if (!Path || EventReference.GetSourceObject() != Attack->Montage || Event->NotifyStateClass.Get() != this ||
			!Attack->GetMeleeTrajectoryFailure(MeshComp->GetSkeletalMeshAsset(), *Path).IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Authored melee path rejected its source/window binding on %s"), *GetNameSafe(OwnerActor));
			return;
		}
	}
	const uint64 Token = HitValidation ? HitValidation->BeginHitWindow() : 0;
	if (HitValidation && !Token) { return; }
	FTraceState& State = TraceStates.FindOrAdd(MeshComp).Add(EventReference.GetNotifyInstanceID(),
		{Path ? FVector::ZeroVector : ResolveTraceLocation(MeshComp), HitValidation, Token, HitValidation ? HitValidation->GetSpatialRevision() : 0});
	if (Path)
	{
		State.bAuthoredPath = true;
		State.AuthoredAttack = Attack;
		State.AuthoredPath = *Path;
		State.ConsumedMontageTime = State.AuthoredPath.WindowStart;
	}
}

void UProject_JAnimNotifyState_MeleeHit::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeNotify);
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (!MeshComp)
	{
		return;
	}
	auto* MeshStates = TraceStates.Find(MeshComp);
	auto* State = MeshStates ? MeshStates->Find(EventReference.GetNotifyInstanceID()) : nullptr;
	if (!State)
	{
		return;
	}
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!CanProduceMeleeContacts(OwnerActor))
	{
		RetireTraceState(MeshComp, EventReference.GetNotifyInstanceID());
		return;
	}
	if (!HitEventTag.IsValid() || (State->WindowToken && (!State->Validation.IsValid() ||
		!State->Validation->IsHitWindowCurrent(State->WindowToken))))
	{
		return;
	}
	// Event handlers may retire objects and collect garbage before returning.
	// Keep the execution's storage alive while still rejecting destroyed owners.
	const TStrongObjectPtr<UProject_JAnimNotifyState_MeleeHit> PinnedNotify(this);
	const TStrongObjectPtr<USkeletalMeshComponent> PinnedMesh(MeshComp);
	const TStrongObjectPtr<AActor> PinnedOwner(OwnerActor);

	UWorld* World = MeshComp->GetWorld();
	if (!World)
	{
		return;
	}

	// Resolve once before event delivery, which may synchronously cancel/change
	// equipment. Never carry this call-local observation across a gameplay event.
	auto* HitValidation = OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>();
	const auto* AttackDefinition = HitValidation ? HitValidation->GetActiveAttackDefinition() : nullptr;

    if (State->bAuthoredPath)
    {
        TickAuthoredPath(MeshComp,EventReference,*State,HitValidation,AttackDefinition);
        return;
    }
	const FVector TraceLocation = ResolveTraceLocation(MeshComp, AttackDefinition);
	if (HitValidation && State->SpatialRevision != HitValidation->GetSpatialRevision())
	{
		// The old endpoint belongs to another physical location. Keep the authored
		// window alive, but wait for a real post-teleport segment before tracing.
		State->PreviousLocation = TraceLocation;
		State->SpatialRevision = HitValidation->GetSpatialRevision();
		if (State->bTemporalPose) State->ConsumedMontageTime = FMath::Max(State->ConsumedMontageTime, EventReference.GetCurrentAnimationTime());
		return;
	}
	FVector TraceStart = State->PreviousLocation, TraceEnd = TraceLocation;
	State->PreviousLocation = TraceLocation;
	FVector PreviousWorld; float PreviousTime = 0;
	if (HitValidation && HitValidation->TryGetPreviousMeleePose(MeshComp, EventReference, PreviousWorld, PreviousTime))
	{
		const auto* Event = EventReference.GetNotify();
		const float CurrentTime = EventReference.GetCurrentAnimationTime();
		if (!State->bTemporalPose) { State->bTemporalPose = true; State->ConsumedMontageTime = Event->GetTriggerTime(); }
		const float From = FMath::Max3(PreviousTime, Event->GetTriggerTime(), State->ConsumedMontageTime);
		const float To = FMath::Min(CurrentTime, Event->GetEndTriggerTime());
		State->ConsumedMontageTime = FMath::Max(State->ConsumedMontageTime, To);
		if (To <= From) return;
		TraceStart = FMath::Lerp(PreviousWorld, TraceLocation, (From - PreviousTime) / (CurrentTime - PreviousTime));
		TraceEnd = FMath::Lerp(PreviousWorld, TraceLocation, (To - PreviousTime) / (CurrentTime - PreviousTime));
	}
	else if (State->bTemporalPose)
	{
		// Do not turn a missing/discontinuous temporal sample into an outside-window chord.
		State->ConsumedMontageTime = FMath::Max(State->ConsumedMontageTime, EventReference.GetCurrentAnimationTime());
		return;
	}
	const FTraceState Execution = *State;
	const int32 InstanceID = EventReference.GetNotifyInstanceID();
	float EffectiveTraceRadius = TraceRadius;
	if (AttackDefinition) { EffectiveTraceRadius = AttackDefinition->HitSpec.TraceRadius; }

	if (OwnerActor->HasAuthority() && HitValidation)
	{
		HitValidation->RecordAuthoritativeTrace(TraceStart, TraceEnd);
	}

	TArray<FHitResult> OutHits;

	// Shared native policy preserves the former Kismet results and metadata.
	// Query storage stays local because event delivery below may reenter this notify.
	const FCollisionQueryParams Params = Project_J::Combat::MakeMeleeSweepParams(OwnerActor);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeSweep);
		Project_J::Combat::SweepMeleeSphere(*World, Params, TraceStart, TraceEnd, EffectiveTraceRadius, OutHits);
	}
	LogAuthorityMeleeSweep(*OwnerActor, 0, EffectiveTraceRadius, TraceStart, TraceEnd, OutHits);

	for (const FHitResult& Hit : OutHits)
	{
		// A hit event can synchronously end this notify, destroy its owner, or
		// start a new attack. Reacquire by execution identity after each callback;
		// neither a map pointer nor an old sweep belongs to the replacement window.
		const auto* CurrentStates = TraceStates.Find(MeshComp);
		const auto* Current = CurrentStates ? CurrentStates->Find(InstanceID) : nullptr;
		if (!IsValid(OwnerActor) || OwnerActor->IsActorBeingDestroyed() || !IsValid(MeshComp) ||
			MeshComp->GetOwner() != OwnerActor || !Current || Current->WindowToken != Execution.WindowToken ||
			Current->Validation != Execution.Validation || (Execution.WindowToken &&
			(!Execution.Validation.IsValid() || !Execution.Validation->IsHitWindowCurrent(Execution.WindowToken) ||
			Execution.Validation->GetSpatialRevision() != Execution.SpatialRevision))) break;
		if (AActor* HitActor = Hit.GetActor())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeDispatch);
			FGameplayEventData Payload;
			Payload.EventTag = HitEventTag;
			Payload.Instigator = OwnerActor;
			Payload.Target = HitActor;
			Payload.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(Hit);

			// Send the hit event to the attacker's ability system
			// The MeleeCombo Ability will catch this, filter duplicates, and apply GameplayEffects
			LogAuthorityMeleeEvent(*OwnerActor, *HitActor);
			UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, HitEventTag, Payload);
		}
	}
}


void UProject_JAnimNotifyState_MeleeHit::TickAuthoredPath(USkeletalMeshComponent* Mesh,const FAnimNotifyEventReference& Reference,FTraceState& State,
    UProject_JCombatHitValidationComponent* Validation,const UProject_JAttackDefinition* Attack)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_AuthoredMeleePath);
    const float CurrentTime=Reference.GetCurrentAnimationTime();
    const int32 OriginalInstanceID=Reference.GetNotifyInstanceID();
    const uint64 OriginalToken=State.WindowToken;
    const auto OriginalValidation=State.Validation;
    const auto Retire=[&](const TCHAR* Reason)
    {
        // A query/plugin callback may have rehashed shared notify storage.
        auto* States=TraceStates.Find(Mesh); auto* Current=States?States->Find(OriginalInstanceID):nullptr;
        if (!Current || Current->WindowToken!=OriginalToken || Current->Validation!=OriginalValidation) return;
        const TStrongObjectPtr<UProject_JCombatHitValidationComponent> Original(OriginalValidation.Get());
        if (FMath::IsFinite(CurrentTime)) Current->ConsumedMontageTime=FMath::Max(Current->ConsumedMontageTime,CurrentTime);
        if (Original) Original->EndHitWindow(OriginalToken);
        UE_LOG(LogTemp,Warning,TEXT("Authored melee path retired without fallback: %s; Owner=%s"),Reason,*GetNameSafe(Mesh->GetOwner()));
    };
    const FAnimNotifyEvent* Event = Reference.GetNotify();
    const FProject_JMeleeTrajectory* Path = Attack && Event ? Attack->FindMeleeTrajectory(*Event) : nullptr;
    if (!Validation || Validation!=State.Validation.Get() || Attack!=State.AuthoredAttack.Get() || !Path ||
        State.AuthoredPath.SourceStamp!=Path->SourceStamp || !FMath::IsFinite(CurrentTime) ||
        !Attack->GetMeleeTrajectoryFailure(Mesh->GetSkeletalMeshAsset(), *Path).IsEmpty())
    { Retire(TEXT("Execution/source data changed")); return; }
    if (State.SpatialRevision!=Validation->GetSpatialRevision())
    { Retire(TEXT("Spatial discontinuity")); return; }
    const float To=FMath::Min(CurrentTime,State.AuthoredPath.WindowEnd);
    if (To<=State.ConsumedMontageTime) return;
    FTransform PreviousRoot,CurrentRoot,MeshToRoot; float PreviousTime=0;
    if (!Validation->TryGetMeleeRootInterval(Mesh,Reference,PreviousRoot,CurrentRoot,MeshToRoot,PreviousTime))
    { Retire(TEXT("No supported current physical-root interval")); return; }
    if (PreviousTime-State.ConsumedMontageTime>UE_KINDA_SMALL_NUMBER)
    { Retire(TEXT("Missing unconsumed physical interval")); return; }
    const float From=FMath::Max(State.ConsumedMontageTime,PreviousTime);
    if (To<=From) return;
    TArray<FProject_JAuthoritativeTraceSegment> Segments;
    if (!State.AuthoredPath.BuildWorldSegments(PreviousTime,CurrentTime,From,To,PreviousRoot,CurrentRoot,MeshToRoot,Segments))
    { Retire(TEXT("Invalid or overflowing authored interval")); return; }
    const float Radius=Attack->HitSpec.TraceRadius;
    if (!FMath::IsFinite(Radius) || Radius<0) { Retire(TEXT("Invalid authored radius")); return; }
    State.ConsumedMontageTime=To; // Detach the entire cursor before queries or any gameplay callback.
    const uint64 Token=State.WindowToken,Spatial=State.SpatialRevision;
    const auto BoundValidation=State.Validation; const auto BoundAttack=State.AuthoredAttack;
    const FString SourceStamp=State.AuthoredPath.SourceStamp;
    const int32 InstanceID=Reference.GetNotifyInstanceID();
    AActor* Owner=Mesh->GetOwner(); UWorld* World=Mesh->GetWorld();
    const bool bAuthority=Owner->HasAuthority();
    const TStrongObjectPtr<UProject_JCombatHitValidationComponent> PinnedValidation(Validation);
    const TStrongObjectPtr<UWorld> PinnedWorld(World);
    const auto IsCurrent=[&]()
    {
        const auto* States=TraceStates.Find(Mesh); const auto* Current=States?States->Find(InstanceID):nullptr;
        return IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->HasAuthority()==bAuthority && IsValid(Mesh) && !Mesh->IsBeingDestroyed() &&
            Mesh->IsRegistered() && Mesh->GetOwner()==Owner && IsValid(World) && !World->bIsTearingDown && Mesh->GetWorld()==World &&
            IsValid(Validation) && !Validation->IsBeingDestroyed() && Current && Current->bAuthoredPath && Current->Validation==BoundValidation &&
            Current->WindowToken==Token && Current->SpatialRevision==Spatial && Current->ConsumedMontageTime==To &&
            Current->AuthoredAttack==BoundAttack && Current->AuthoredPath.SourceStamp==SourceStamp && Validation->IsHitWindowCurrent(Token) &&
            Validation->GetSpatialRevision()==Spatial && Validation->GetActiveAttackDefinition()==BoundAttack.Get();
    };
    struct FContact { FHitResult Hit; int32 Segment=0; };
    TArray<FContact> Contacts;
    TArray<FHitResult> Hits;
    TSet<TWeakObjectPtr<const AActor>> SeenActors;
    const FCollisionQueryParams Params=Project_J::Combat::MakeMeleeSweepParams(Owner);
    for (int32 I=0;I<Segments.Num();++I)
    {
        if (!IsCurrent()) return;
        { TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Combat_MeleeSweep);
          Project_J::Combat::SweepMeleeSphere(*World,Params,Segments[I].TraceStart,Segments[I].TraceEnd,Radius,Hits); }
        LogAuthorityMeleeSweep(*Owner, I, Radius, Segments[I].TraceStart, Segments[I].TraceEnd, Hits);
        if (!IsCurrent()) return;
        for (const auto& Hit:Hits)
        {
            auto* Victim=Hit.GetActor();
            if (!IsValid(Victim) || SeenActors.Contains(Victim)) continue;
            // Coalesce the first eligible contact, not a blocked earlier piece.
            // Otherwise splitting this same path over frames changes NPC hits.
            const bool bBlocked=Validation->IsTargetObstructedFrom(Victim,Segments[I].TraceStart);
            if (!IsCurrent()) return;
            if (bBlocked) continue;
            SeenActors.Add(Victim); FContact& Contact=Contacts.AddDefaulted_GetRef(); Contact.Hit=Hit; Contact.Segment=I;
        }
    }
    // The packet denotes queries already completed, never a future/unexecuted remainder.
    const uint64 Packet=bAuthority?Validation->RecordAuthoritativeTracePacket(Segments,Token,Spatial):0;
    if (bAuthority && !Packet) { if (IsCurrent()) Retire(TEXT("Authoritative packet rejected")); return; }
    for (const auto& Contact:Contacts)
    {
        if (!IsCurrent() || (bAuthority && !Validation->SelectAuthoritativeTraceSegment(Packet,Token,Spatial,Contact.Segment))) break;
        auto* Victim=Contact.Hit.GetActor();
        if (!IsValid(Victim) || Victim->IsActorBeingDestroyed()) continue;
        FGameplayEventData Payload; Payload.EventTag=HitEventTag; Payload.Instigator=Owner; Payload.Target=Victim;
        Payload.TargetData=UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(Contact.Hit);
        LogAuthorityMeleeEvent(*Owner, *Victim);
        UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner,HitEventTag,Payload);
    }
}

FVector UProject_JAnimNotifyState_MeleeHit::ResolveTraceLocation(USkeletalMeshComponent* MeshComp) const
{
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	// Both the predicted sweep and server rewind history use this Leader-pose
	// endpoint. The presentation weapon and follower/IK pose are cosmetic only.
	const AActor* OwnerActor = MeshComp->GetOwner();
	const UProject_JCombatHitValidationComponent* HitValidation = OwnerActor
		? OwnerActor->FindComponentByClass<UProject_JCombatHitValidationComponent>() : nullptr;
	const UProject_JAttackDefinition* AttackDefinition = HitValidation ? HitValidation->GetActiveAttackDefinition() : nullptr;
	return ResolveTraceLocation(MeshComp, AttackDefinition);
}

FVector UProject_JAnimNotifyState_MeleeHit::ResolveTraceLocation(USkeletalMeshComponent* MeshComp, const UProject_JAttackDefinition* AttackDefinition) const
{
	if (!MeshComp) return FVector::ZeroVector;
	const FVector LocalTip = AttackDefinition && AttackDefinition->HitSpec.bUseCanonicalBladeTipOffset
		? AttackDefinition->HitSpec.CanonicalBladeTipOffset : FVector::ZeroVector;
	return MeshComp->GetSocketTransform(SocketName, RTS_World).TransformPosition(LocalTip);
}

void UProject_JAnimNotifyState_MeleeHit::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	RetireTraceState(MeshComp, EventReference.GetNotifyInstanceID());
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

void UProject_JAnimNotifyState_MeleeHit::RetireTraceState(USkeletalMeshComponent* MeshComp, int32 NotifyInstanceID)
{
	if (auto* MeshStates = TraceStates.Find(MeshComp))
	{
		FTraceState State;
		if (MeshStates->RemoveAndCopyValue(NotifyInstanceID, State))
		{
			if (MeshStates->IsEmpty()) { TraceStates.Remove(MeshComp); }
			if (auto* HitValidation = State.Validation.Get()) { HitValidation->EndHitWindow(State.WindowToken); }
		}
	}
}
