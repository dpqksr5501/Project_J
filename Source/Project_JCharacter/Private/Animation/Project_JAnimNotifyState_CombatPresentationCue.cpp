#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/Project_JGameplayAbility_Melee.h"

#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatPresentationNotify, Log, All);

void UProject_JAnimNotifyState_CombatPresentationCue::BeginDestroy()
{
    FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
    PostGCHandle.Reset();
    // Destruction may run inside GC. Retire only local weak storage here;
    // normal attack/owner teardown remains the component's primary cleanup.
    Leases.Reset();
    Super::BeginDestroy();
}

void UProject_JAnimNotifyState_CombatPresentationCue::EndDetachedLease(const FCueLease& Lease)
{
    if (!Lease.Token) return;
    const TStrongObjectPtr<UProject_JCombatPresentationComponent> Presentation(Lease.Presentation.Get());
    if (!Presentation.IsValid() || !IsValid(Presentation->GetOwner()) || !IsValid(Presentation->GetWorld())) return;
    const TStrongObjectPtr<AActor> Owner(Presentation->GetOwner());
    const TStrongObjectPtr<UWorld> World(Presentation->GetWorld());
    Presentation->EndCueLease(Lease.Token);
}

void UProject_JAnimNotifyState_CombatPresentationCue::PruneCollectedMeshes()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatVFX_WeakMeshPrune);
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepNotify(this);
    TArray<FCueLease> Retired;
    for (auto It = Leases.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            for (const auto& Pair : It.Value()) if (Pair.Value.Token) Retired.Add(Pair.Value);
            It.RemoveCurrent();
        }
    }
    // PostGC runs after GC/hash locks are released and GIsGarbageCollecting is
    // false. Detach everything before native Niagara can reenter or run GC.
    TArray<TStrongObjectPtr<UObject>> Pins;
    Pins.Reserve(Retired.Num() * 3);
    for (const FCueLease& Lease : Retired)
    {
        if (auto* Presentation = Lease.Presentation.Get())
        {
            Pins.Emplace(Presentation);
            if (IsValid(Presentation->GetOwner())) Pins.Emplace(Presentation->GetOwner());
            if (IsValid(Presentation->GetWorld())) Pins.Emplace(Presentation->GetWorld());
        }
    }
    for (const FCueLease& Lease : Retired) EndDetachedLease(Lease);
}

void UProject_JAnimNotifyState_CombatPresentationCue::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatVFX_NotifyBegin);
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepNotify(this);
    const TStrongObjectPtr<USkeletalMeshComponent> KeepMesh(IsValid(MeshComp) ? MeshComp : nullptr);
    const TStrongObjectPtr<UAnimSequenceBase> KeepAnimation(IsValid(Animation) ? Animation : nullptr);
    Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
    AActor* OwnerActor = KeepMesh.IsValid() ? KeepMesh->GetOwner() : nullptr;
    if (!IsValid(KeepMesh.Get()) || !IsValid(OwnerActor) || OwnerActor->IsActorBeingDestroyed() || KeepMesh->IsBeingDestroyed() ||
        HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    {
        UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify begin ignored: mesh or owner missing/retired. Animation=%s Cue=%s"),
            *GetNameSafe(Animation), *CueTag.ToString());
        return;
    }
    if (Purpose != EProject_JCombatCuePurpose::GameplayWarning &&
        !UProject_JGameplayAbility_Melee::CanDispatchAttackNotify(*KeepMesh.Get(), EventReference)) { return; }
    const TStrongObjectPtr<AActor> KeepOwner(OwnerActor);
    auto* Presentation = OwnerActor->FindComponentByClass<UProject_JCombatPresentationComponent>();
    if (!IsValid(Presentation) || Presentation->IsBeingDestroyed())
    {
        UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify begin ignored: presentation component missing. Owner=%s Animation=%s Cue=%s"),
            *GetNameSafe(OwnerActor), *GetNameSafe(Animation), *CueTag.ToString());
        return;
    }
    const TStrongObjectPtr<UProject_JCombatPresentationComponent> KeepPresentation(Presentation);
    const TStrongObjectPtr<UWorld> KeepWorld(KeepMesh->GetWorld());
    if (!KeepWorld.IsValid() || KeepWorld->bIsTearingDown || Presentation->GetWorld() != KeepWorld.Get()) return;
    const bool bMeshRegistered = KeepMesh->IsRegistered();
    const bool bPresentationRegistered = Presentation->IsRegistered();
    const TWeakObjectPtr<USkeletalMeshComponent> MeshKey(KeepMesh.Get());
    const FGameplayTag OriginalCue = CueTag;
    const EProject_JCombatCuePurpose OriginalPurpose = Purpose;
    const int32 ID = EventReference.GetNotifyInstanceID();
    UE_LOG(LogProjectJCombatPresentationNotify, Verbose, TEXT("[CombatVFX] Notify begin. Owner=%s Animation=%s Cue=%s Duration=%.3f"),
        *GetNameSafe(OwnerActor), *GetNameSafe(Animation), *OriginalCue.ToString(), TotalDuration);
    if (!PostGCHandle.IsValid()) PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ThisClass::PruneCollectedMeshes);
    {
        // Only this participant's stale executions are inspected. Pending claims
        // survive recursive Begin and prevent a second acquire for the same ID.
        if (auto* MeshLeases = Leases.Find(MeshKey))
        {
            for (auto It = MeshLeases->CreateIterator(); It; ++It)
            {
                const FCueLease& Lease = It.Value();
                if (!Lease.Presentation.IsValid() || Lease.Presentation->IsBeingDestroyed() ||
                    (!Lease.Token && !Lease.Claim) || (Lease.Token && !Lease.Presentation->IsCueLeaseCurrent(Lease.Token))) It.RemoveCurrent();
            }
            if (MeshLeases->Contains(ID)) return;
        }
    }
    // Reject exhaustion instead of allowing an old in-flight claim to become
    // indistinguishable from a new End->Begin admission after integer wrap.
    if (NextLeaseClaim == MAX_uint64) return;
    const uint64 Claim = ++NextLeaseClaim;
    Leases.FindOrAdd(MeshKey).Add(ID, {Presentation, 0, Claim});
    const uint64 Token = Presentation->BeginCueLease(OriginalCue, OriginalPurpose == EProject_JCombatCuePurpose::GameplayWarning);
    bool bPublished = false;
    {
        auto* MeshLeases = Leases.Find(MeshKey);
        auto* Current = MeshLeases ? MeshLeases->Find(ID) : nullptr;
        const bool bOwnClaim = Current && Current->Claim == Claim && Current->Presentation.Get() == Presentation && !Current->Token;
        const bool bCurrentSource = IsValid(this) && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
            IsValid(KeepMesh.Get()) && !KeepMesh->IsBeingDestroyed() && KeepMesh->GetOwner() == KeepOwner.Get() &&
            KeepMesh->GetWorld() == KeepWorld.Get() && KeepMesh->IsRegistered() == bMeshRegistered &&
            IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed() && !KeepWorld->bIsTearingDown &&
            IsValid(Presentation) && !Presentation->IsBeingDestroyed() && Presentation->GetOwner() == KeepOwner.Get() &&
            Presentation->GetWorld() == KeepWorld.Get() && Presentation->IsRegistered() == bPresentationRegistered &&
            KeepOwner->FindComponentByClass<UProject_JCombatPresentationComponent>() == Presentation && CueTag == OriginalCue && Purpose == OriginalPurpose;
        if (bOwnClaim && bCurrentSource && Token && Presentation->IsCueLeaseCurrent(Token))
        {
            Current->Token = Token;
            bPublished = true;
        }
        else if (bOwnClaim)
        {
            MeshLeases->Remove(ID);
            if (MeshLeases->IsEmpty()) Leases.Remove(MeshKey);
        }
    }
    // End/replace/GC during acquisition must retire only this returned token,
    // never a replacement claim or its newly acquired same-cue ownership.
    if (!bPublished && Token) EndDetachedLease({Presentation, Token, Claim});
}

void UProject_JAnimNotifyState_CombatPresentationCue::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatVFX_NotifyEnd);
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepNotify(this);
    const TStrongObjectPtr<USkeletalMeshComponent> KeepMesh(IsValid(MeshComp) ? MeshComp : nullptr);
    const TStrongObjectPtr<UAnimSequenceBase> KeepAnimation(IsValid(Animation) ? Animation : nullptr);
    FCueLease Lease;
    {
        if (auto* MeshLeases = Leases.Find(MeshComp))
        {
            MeshLeases->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), Lease);
            if (MeshLeases->IsEmpty()) Leases.Remove(MeshComp);
        }
    }
    EndDetachedLease(Lease);
    Super::NotifyEnd(MeshComp, Animation, EventReference);
}
