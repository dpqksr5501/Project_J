#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/AnimInstance.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Movement/Project_JMovementQueries.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEffectType.h"
#include "NiagaraWorldManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "Misc/FileHelper.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool MixedFXPolicy(const UNiagaraSystem* System, const UNiagaraEffectType* Type)
{
    if (!IsValid(System) || !IsValid(Type) || System->GetEffectType()!=Type || System->MaxPoolSize!=32) return false;
    const auto& S=System->GetScalabilitySettings(); const auto& T=Type->GetActiveSystemScalabilitySettings();
    const auto* Handler=Type->GetSignificanceHandler();
    return S.bCullMaxInstanceCount && T.bCullMaxInstanceCount && S.MaxInstances==128 && T.MaxInstances==128
        && !S.bCullPerSystemMaxInstanceCount && !T.bCullPerSystemMaxInstanceCount
        && !S.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse && !T.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse
        && S.bCullByDistance && FMath::IsNearlyEqual(S.MaxDistance,2500.f)
        && Type->UpdateFrequency==ENiagaraScalabilityUpdateFrequency::High && Type->CullReaction==ENiagaraCullReaction::DeactivateImmediateResume
        && Handler && Handler->IsA<UNiagaraSignificanceHandlerDistance>() && System->NeedsSortedSignificanceCull()
        && System->IsAllowedByScalability() && !Type->bAllowCullingForLocalPlayers && !System->AllowCullingForLocalPlayers();
}
bool InMixedFXView(const FMatrix& Projection, const FVector& Location)
{
    const FVector4 P=Projection.TransformFVector4(FVector4(Location,1));
    return !P.ContainsNaN() && P.W>0 && FMath::Abs(P.X)<=P.W && FMath::Abs(P.Y)<=P.W && P.Z>=0 && P.Z<=P.W;
}
}

void UProject_JRenderedCrowdFixture::FFXResourceViewTick::ExecuteTick(float,ELevelTick,ENamedThreads::Type,const FGraphEventRef&)
{
    if (auto* Fixture=Owner.Get()) Fixture->CaptureFXResourceView(Epoch);
}
FString UProject_JRenderedCrowdFixture::FFXResourceViewTick::DiagnosticMessage()
{
    return TEXT("ProjectJ mixed FX native view observation");
}
bool UProject_JRenderedCrowdFixture::IsFXResourceCurrent(uint64 Epoch) const
{
    return FXResourceCount==132 && bFXResourceSetup && !bFXResourceCleaning && !bFXResourceCleaned && Epoch==FXResourceEpoch
        && FXResourceWorld.IsValid() && FXResourceWorld.Get()==GetWorld() && !FXResourceWorld->bIsTearingDown
        && FXResourceViewer.IsValid() && !FXResourceViewer->IsActorBeingDestroyed() && FXResourceViewer->GetWorld()==GetWorld()
        && FXResourceCamera.IsValid() && !FXResourceCamera->IsActorBeingDestroyed() && FXResourceCamera->GetWorld()==GetWorld()
        && FXResourceViewer->GetViewTarget()==FXResourceCamera.Get();
}
bool UProject_JRenderedCrowdFixture::SetupFXResourceCohort(FString& Failure)
{
    check(IsInGameThread());
    if (!FXResourceCount) return true;
    if (FXResourceCount!=132 || NPCCombatCount!=32 || CrowdCount!=200 || !bSimulateMovement || !bWithLocalPlayer || bNetworkObserver
        || bFXResourceSetup || !GetWorld() || GetWorld()->GetNetMode()!=NM_Standalone || GetWorld()->bIsTearingDown)
    { Failure=TEXT("Mixed FX requires fresh standalone200/local/movement/NPC32 and exactly132 cosmetic owners"); return false; }
#if !WITH_NIAGARA_DEBUGGER
    Failure=TEXT("Native read-only Niagara cull-reason observation is unavailable"); return false;
#endif
    const TStrongObjectPtr<ThisClass> Self(this);
    FXResourceWorld=GetWorld(); FXResourceViewer=GetWorld()->GetFirstPlayerController();
    FXResourceCamera=FXResourceViewer.IsValid()?Cast<ACameraActor>(FXResourceViewer->GetViewTarget()):nullptr;
    bFXResourceSetup=true; const uint64 Epoch=++FXResourceEpoch;
    if (!IsFXResourceCurrent(Epoch)) { Failure=TEXT("Existing native camera/local observer is unavailable"); return false; }
    FXResourceSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Effects/ProjectJ/Combat/NS_CombatTrail.NS_CombatTrail"));
    FXResourceType=LoadObject<UNiagaraEffectType>(nullptr,TEXT("/Game/Effects/ProjectJ/Combat/NET_CombatTrail.NET_CombatTrail"));
    const auto* Pool=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.Pool"));
    if (!IsFXResourceCurrent(Epoch) || !MixedFXPolicy(FXResourceSystem,FXResourceType) || !Pool || !Pool->GetInt())
    { Failure=TEXT("Unchanged saved128 trail/EffectType/pool32 policy is unavailable"); return false; }
    bFXResourceSystemDirty=FXResourceSystem->GetPackage()->IsDirty(); bFXResourceTypeDirty=FXResourceType->GetPackage()->IsDirty();
    FXResourceAttackTag=FGameplayTag::RequestGameplayTag(TEXT("Attack.Greatsword.LMB.1"),false);
    FXResourceCueTag=FGameplayTag::RequestGameplayTag(TEXT("PresentationCue.Combat.Trail"),false);
    FXResourceSet=NewObject<UProject_JCombatPresentationSet>(this,NAME_None,RF_Transient);
    FXResourceProfile=NewObject<UProject_JAttackPresentationProfile>(FXResourceSet,NAME_None,RF_Transient);
    if (!IsFXResourceCurrent(Epoch) || !FXResourceAttackTag.IsValid() || !FXResourceCueTag.IsValid() || !FXResourceSet || !FXResourceProfile)
    { Failure=TEXT("Transient resource-only cue wrapper could not be constructed"); return false; }
    auto& Cue=FXResourceProfile->Cues.AddDefaulted_GetRef(); Cue.CueTag=FXResourceCueTag; Cue.NiagaraSystem=FXResourceSystem;
    Cue.AttachmentTarget=EProject_JCombatVFXAttachmentTarget::CharacterMesh; Cue.bLooping=true; Cue.bDestroyImmediatelyOnStop=true;
    if (!Cue.bUseRemoteSpawnBudget) { Failure=TEXT("Resource cue lost its ordinary remote-attempt admission policy"); return false; }
    auto& Entry=FXResourceSet->AttackPresentations.AddDefaulted_GetRef(); Entry.AttackTag=FXResourceAttackTag; Entry.Profile=FXResourceProfile;
    FXResourceCSV=TEXT("phase,frame,observed_frame,world_time,leases,components,distinct,active,culled,count_culled,other_culled,pending_components,recently_rendered,player_montages,npc_montages,budget_frame,budget_current,budget_frame_attempts,budget_applied_total,budget_rejected_total,budget_pending,asset_leases,asset_groups,asset_inflight,view_count,view_match,retired_invalid,retired_pool,retired_reused,pending_retirement\n");
    FXResourceViewTick.Owner=this; FXResourceViewTick.Epoch=Epoch;
    FXResourceViewTick.bCanEverTick=true; FXResourceViewTick.bStartWithTickEnabled=true; FXResourceViewTick.bRunOnAnyThread=false;
    FXResourceViewTick.TickGroup=FXResourceViewTick.EndTickGroup=TG_PostUpdateWork;
    FXResourceViewTick.RegisterTickFunction(GetWorld()->PersistentLevel); FXResourceViewTick.SetTickFunctionEnable(true);
    RecordFXResourceCheckpoint(TEXT("setup_warm"));
    return true;
}
void UProject_JRenderedCrowdFixture::CaptureFXResourceView(uint64 Epoch)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RenderedCrowd_FXResourceView);
    check(IsInGameThread());
    if (!IsFXResourceCurrent(Epoch)) return;
    const TStrongObjectPtr<ThisClass> Self(this);
    FFXResourceView Next; Next.Frame=GFrameCounter; Next.WorldTime=GetWorld()->GetTimeSeconds(); Next.Target=FXResourceViewer->GetViewTarget();
    FRotator Rotation; FXResourceViewer->GetPlayerViewPoint(Next.Camera,Rotation); Next.Rotation=Rotation.Quaternion();
    auto* Local=FXResourceViewer->GetLocalPlayer(); FSceneViewProjectionData Projection;
    Next.bProjection=Local && Local->ViewportClient && Local->ViewportClient->Viewport && Local->GetProjectionData(Local->ViewportClient->Viewport,Projection);
    if (Next.bProjection) Next.Projection=Projection.ComputeViewProjectionMatrix();
    auto* Native=FNiagaraWorldManager::Get(GetWorld());
    if (Native)
    {
        const auto Views=Native->GetCachedViewInfo(); Next.ViewCount=Views.Num();
        for (const auto& View:Views) Next.bCameraMatch|=View.ViewToWorld.GetOrigin().Equals(Next.Camera,.1);
    }
    if (!IsFXResourceCurrent(Epoch)) return;
    Next.Slots.SetNum(FXResourceRows.Num());
    for (int32 I=0;I<FXResourceRows.Num();++I)
    {
        const auto& Row=FXResourceRows[I]; auto* Cue=Row.Cue.Get();
        auto* N=bFXResourceStopping?Row.Component.Get():(Cue?Cue->GetCueLeaseComponent(Row.Lease).Get():nullptr);
        if (!IsValid(N) || N->IsBeingDestroyed()) continue;
        auto& Slot=Next.Slots[I]; Slot.ComponentId=N->GetUniqueID(); Slot.Location=N->GetComponentLocation();
        if (Native && Next.ViewCount) Slot.Distance=Native->GetLODDistance(Slot.Location);
        Slot.bActive=N->IsActive(); Slot.bComplete=N->IsComplete(); Slot.bRegistered=N->IsRegisteredWithScalabilityManager();
        Slot.bLocal=N->IsLocalPlayerEffect(); Slot.bRecentlyRendered=N->WasRecentlyRendered(.2f); Slot.Pool=int32(N->PoolingMethod);
#if WITH_NIAGARA_DEBUGGER
        const auto& State=N->DebugCachedScalabilityState;
        Slot.Culled=State.bCulled; Slot.CountCull=State.bCulledByInstanceCount;
        Slot.OtherCull=State.bCulledByDistance || State.bCulledByVisibility || State.bCulledByGlobalBudget;
#endif
    }
    if (IsFXResourceCurrent(Epoch)) FXResourceView=MoveTemp(Next);
}
bool UProject_JRenderedCrowdFixture::BeginFXResourceCohort(FString& Failure)
{
    const uint64 Epoch=FXResourceEpoch;
    if (!IsFXResourceCurrent(Epoch) || bFXResourceStarted || !FXResourceRows.IsEmpty())
    { Failure=TEXT("FX resource start lost its original world/view scope"); return false; }
    const TStrongObjectPtr<ThisClass> Self(this);
    const auto* Defaults=GetDefault<AProject_JNPCCharacter>();
    if (!Defaults || !Defaults->GetMesh() || Defaults->GetMesh()->GetAttachParent()!=Defaults->GetRootComponent() || Defaults->GetMesh()->GetRelativeLocation().ContainsNaN())
    { Failure=TEXT("Native cosmetic host has no finite root-relative mesh origin"); return false; }
    const FVector MeshOffset=Defaults->GetMesh()->GetRelativeLocation();
    bFXResourceStarted=true; FXResourceCameraOrigin=FXResourceView.Camera; FXResourceCameraRotation=FXResourceView.Rotation;
    FXResourceRows.SetNum(132);
    for (int32 I=0;I<132;++I)
    {
        const FVector Offset(400.+1400.*I/131.,20.*((I%11)-5),12.*(I/11-5.5));
        const FVector EffectPosition=FXResourceCameraOrigin+FXResourceCameraRotation.RotateVector(Offset);
        const FVector Position=EffectPosition-FXResourceCameraRotation.RotateVector(MeshOffset);
        if (!InMixedFXView(FXResourceView.Projection,EffectPosition)) { Failure=TEXT("Resource attachment would lie outside the unchanged current view"); return false; }
        const FTransform Transform(FXResourceCameraRotation,Position);
        const TStrongObjectPtr<AProject_JNPCCharacter> Owner(GetWorld()->SpawnActorDeferred<AProject_JNPCCharacter>(
            AProject_JNPCCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn));
        if (!Owner) { Failure=TEXT("Cosmetic-only native owner spawn failed"); return false; }
        if (!IsFXResourceCurrent(Epoch)) { if (!Owner->IsActorBeingDestroyed()) Owner->Destroy(); Failure=TEXT("FX owner creation retired its scope"); return false; }
        FXResourceRows[I].Owner=Owner.Get(); FXResourceRows[I].OwnerId=Owner->GetUniqueID(); FXResourceRows[I].Expected=Position;
        Owner->FinishSpawning(Transform);
        if (!IsFXResourceCurrent(Epoch) || Owner->IsActorBeingDestroyed()) { Failure=TEXT("FX owner BeginPlay retired its scope"); return false; }
        Owner->SetActorEnableCollision(false);
        if (!Project_J::Movement::SetMovementMode(Owner.Get(),MOVE_None) || !IsFXResourceCurrent(Epoch) || Owner->GetController() ||
            !Owner->GetMesh() || Owner->GetMesh()->GetSkeletalMeshAsset() || !Owner->GetMesh()->GetComponentLocation().Equals(EffectPosition,.1) ||
            !InMixedFXView(FXResourceView.Projection,Owner->GetMesh()->GetComponentLocation()))
        { Failure=TEXT("Cosmetic owner is not an unpossessed meshless native MOVE_None resource host"); return false; }
        FXResourceRows[I].Mesh=Owner->GetMesh();
        const TStrongObjectPtr<UProject_JCombatPresentationComponent> Cue(NewObject<UProject_JCombatPresentationComponent>(Owner.Get()));
        if (!IsFXResourceCurrent(Epoch)) { if (Cue) Cue->DestroyComponent(); Failure=TEXT("Cue construction retired its scope"); return false; }
        FXResourceRows[I].Cue=Cue.Get(); Cue->BasePresentationSet=FXResourceSet; Owner->AddInstanceComponent(Cue.Get()); Cue->RegisterComponent();
        if (!IsFXResourceCurrent(Epoch) || !Cue->IsRegistered() || Cue->IsBeingDestroyed()) { Failure=TEXT("Cue registration retired its scope"); return false; }
        Cue->BeginAttackPresentation(FXResourceAttackTag);
        if (!IsFXResourceCurrent(Epoch) || Cue->GetActiveAttackTag()!=FXResourceAttackTag) { Failure=TEXT("Resource presentation identity was replaced"); return false; }
        const uint64 Lease=Cue->BeginCueLease(FXResourceCueTag);
        if (!IsFXResourceCurrent(Epoch)) { Cue->EndCueLease(Lease); Failure=TEXT("Cue admission retired its scope"); return false; }
        FXResourceRows[I].Lease=Lease;
        if (!Lease || !Cue->IsCueLeaseCurrent(Lease)) { Failure=TEXT("Resource loop did not receive its actual lease"); return false; }
    }
    RecordFXResourceCheckpoint(TEXT("phase3_leases_queued"));
    return true;
}
bool UProject_JRenderedCrowdFixture::ObserveFXResourceCohort(FString& Failure)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RenderedCrowd_FXResourceObserver);
    check(IsInGameThread());
    if (!FXResourceCount || bFXResourceCleaned) return true;
    const uint64 Epoch=FXResourceEpoch;
    if (bFXResourceViolation) { Failure=FXResourceFailure; return false; }
    if (!IsFXResourceCurrent(Epoch) || !MixedFXPolicy(FXResourceSystem,FXResourceType))
    { Failure=FXResourceFailure=TEXT("Mixed FX resource source/view/policy lifetime changed"); bFXResourceViolation=true; return false; }
    const auto& V=FXResourceView;
    if (V.Frame==MAX_uint64 || V.Frame>GFrameCounter || GFrameCounter-V.Frame>1 || V.Frame==FXResourceLastObservedFrame ||
        !V.bProjection || !V.bCameraMatch || V.ViewCount<1 || V.Target.Get()!=FXResourceCamera.Get()) return true;
    FXResourceLastObservedFrame=V.Frame;
    if (Phase!=FXResourceLastPhase) { FXResourceLastPhase=Phase; RecordFXResourceCheckpoint(TEXT("phase_boundary")); }
    if (!bFXResourceStarted)
    {
        if (Phase<3) return true;
        if (Phase!=3) { Failure=TEXT("Resource cohort missed its original phase3 start"); return false; }
        return BeginFXResourceCohort(Failure);
    }
    if (++FXResourceSamples>65536) { Failure=TEXT("FX aggregate observation bound exceeded"); return false; }
    FFXResourceCounts Counts; TSet<UNiagaraComponent*> Seen; Seen.Reserve(132);
    for (int32 I=0;I<FXResourceRows.Num();++I)
    {
        auto& R=FXResourceRows[I]; auto* Owner=R.Owner.Get(); auto* Cue=R.Cue.Get();
        if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->GetWorld()!=GetWorld() || Owner->GetController() || Owner->GetActorEnableCollision() ||
            !Owner->GetActorLocation().Equals(R.Expected,.1) || Owner->GetMesh()!=R.Mesh.Get() || !R.Mesh.IsValid() || R.Mesh->GetSkeletalMeshAsset() ||
            !IsValid(Cue) || !Cue->IsRegistered() || Cue->IsBeingDestroyed() || Cue->GetOwner()!=Owner || Cue->BasePresentationSet!=FXResourceSet)
        { Failure=TEXT("A cosmetic resource owner or its exact cue/mesh identity changed"); return false; }
        const bool Current=Cue->IsCueLeaseCurrent(R.Lease); auto* N=Cue->GetCueLeaseComponent(R.Lease).Get();
        if (!bFXResourceStopping)
        {
            if (!Current || Cue->GetActiveAttackTag()!=FXResourceAttackTag) { Failure=TEXT("Resource lease retired before the phase6 stop"); return false; }
            ++Counts.Current;
            if (!N || !V.Slots.IsValidIndex(I) || V.Slots[I].ComponentId!=N->GetUniqueID()) continue; // Real queue/observation still pending.
            const auto& S=V.Slots[I];
            if (Seen.Contains(N) || (R.bSeen && R.Component.Get()!=N) || N->GetWorld()!=GetWorld() || N->GetAsset()!=FXResourceSystem ||
                N->GetAttachParent()!=R.Mesh.Get() || N->PoolingMethod!=ENCPoolMethod::ManualRelease || S.bLocal ||
                !FMath::IsFinite(S.Distance) || S.Distance<0 || S.Distance>=2500 || !InMixedFXView(V.Projection,S.Location))
            { Failure=FString::Printf(TEXT("FX slot%d qualification failed: duplicate=%d replaced=%d world=%d system=%d parent=%d pool=%d local=%d distance=%.6f in_view=%d registered=%d"),
                I,Seen.Contains(N),R.bSeen && R.Component.Get()!=N,N->GetWorld()==GetWorld(),N->GetAsset()==FXResourceSystem,N->GetAttachParent()==R.Mesh.Get(),
                int32(N->PoolingMethod),S.bLocal,S.Distance,InMixedFXView(V.Projection,S.Location),S.bRegistered); return false; }
            Seen.Add(N); R.Component=N; R.ComponentId=N->GetUniqueID(); R.bSeen=true;
            if (!S.bRegistered) continue; // Native activation may still be compiling before scalability registration.
            ++Counts.Components;
            Counts.Active+=S.bActive && !S.bComplete; Counts.Culled+=S.Culled==1; Counts.CountCull+=S.CountCull==1;
            Counts.OtherCull+=S.OtherCull==1; Counts.RecentlyRendered+=S.bRecentlyRendered;
        }
        else
        {
            if (Current || N || Cue->GetActiveAttackTag().IsValid()) { Failure=TEXT("An old resource lease resumed after exact stop"); return false; }
            N=R.Component.Get(); R.Retirement=0;
            if (!IsValid(N) || N->IsBeingDestroyed()) { ++Counts.Invalid; R.Retirement=1; }
            else if (!N->IsActive() && !N->IsRegisteredWithScalabilityManager() && N->PoolingMethod==ENCPoolMethod::FreeInPool)
            { ++Counts.Pooled; R.Retirement=2; }
            else
            {
                auto* Parent=N->GetAttachParent(); auto* Other=Parent?Parent->GetOwner():nullptr;
                const bool CohortOwner=FXResourceRows.ContainsByPredicate([&](const FFXResourceRow& Row){ return Row.Owner.Get()==Other; });
                if (IsValid(Parent) && Parent->IsRegistered() && IsValid(Other) && !Other->IsActorBeingDestroyed() && !CohortOwner && Other->GetWorld()==GetWorld()
                    && N->GetAsset()==FXResourceSystem && (N->PoolingMethod==ENCPoolMethod::ManualRelease || N->PoolingMethod==ENCPoolMethod::AutoRelease))
                { ++Counts.Reused; R.Retirement=3; }
                else ++Counts.PendingRetirement;
            }
        }
    }
    Counts.Distinct=Seen.Num(); FXResourceCounts=Counts; FXResourcePeakDistinct=FMath::Max(FXResourcePeakDistinct,Counts.Distinct);
    const bool AllNow=Counts.Current==132 && Counts.Components==132 && Counts.Distinct==132;
    if (AllNow && !bFXResourceAllSeen) { bFXResourceAllSeen=true; RecordFXResourceCheckpoint(TEXT("all132_actual_components")); }
    bFXResourceSawActive|=Counts.Active>0; bFXResourceSawCountCull|=Counts.CountCull>0;
    int32 PlayerMontages=0,NPCMontages=0;
    for (const auto& P:Players) if (IsValid(P.Get()) && P->GetMesh() && P->GetMesh()->GetAnimInstance() && P->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()) ++PlayerMontages;
    for (const auto& R:NPCCombatRows) if (R.bAdmitted && !R.bEnded && R.Anim.IsValid() && R.Anim->IsAnyMontagePlaying()) ++NPCMontages;
    if (Phase==5 && AllNow && Counts.Active>0 && Counts.CountCull>0 && PlayerMontages>0 && NPCMontages>0 && !bFXResourceOverlap)
    { bFXResourceOverlap=true; RecordFXResourceCheckpoint(TEXT("phase5_real_montage_overlap")); }
    auto* Budget=GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>(); auto* Assets=GetWorld()->GetSubsystem<UProject_JVisualAssetSubsystem>();
    if (!Budget || !Assets) { Failure=TEXT("Existing resource budget/asset service retired"); return false; }
    const auto& B=Budget->GetStats(); const auto& A=Assets->GetStats();
    FXResourceCSV+=FString::Printf(TEXT("%d,%llu,%llu,%.9f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%d,%d,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"),
        Phase,uint64(GFrameCounter),V.Frame,V.WorldTime,Counts.Current,Counts.Components,Counts.Distinct,Counts.Active,Counts.Culled,Counts.CountCull,Counts.OtherCull,132-Counts.Components,Counts.RecentlyRendered,
        PlayerMontages,NPCMontages,B.Frame,B.Frame==GFrameCounter,B.FrameApplications,B.Applied,B.Rejected,B.Pending,Assets->GetLeaseCount(),Assets->GetGroupCount(),A.InFlight,V.ViewCount,V.bCameraMatch,Counts.Invalid,Counts.Pooled,Counts.Reused,Counts.PendingRetirement);
    if (B.FrameApplications>UProject_JPresentationBudgetSubsystem::MaxApplications) { Failure=TEXT("Existing shared creation-attempt budget exceeded"); return false; }
    if (Phase>=6 && !bFXResourceStopping)
    {
        if (!bFXResourceAllSeen || !bFXResourceSawActive || !bFXResourceSawCountCull || !bFXResourceOverlap)
        { Failure=TEXT("Resource cohort lacks actual132/count-cull/active/montage-overlap evidence"); return false; }
        bFXResourceStopping=true; FXResourceStopAt=FPlatformTime::Seconds();
        for (int32 I=0;I<FXResourceRows.Num();++I)
        {
            const TStrongObjectPtr<UProject_JCombatPresentationComponent> Cue(FXResourceRows[I].Cue.Get()); const uint64 Lease=FXResourceRows[I].Lease;
            if (Cue) { Cue->EndCueLease(Lease); if (IsFXResourceCurrent(Epoch)) Cue->EndAttackPresentation(); }
            if (!IsFXResourceCurrent(Epoch)) { Failure=TEXT("Resource stop callback retired its original scope"); return false; }
        }
        RecordFXResourceCheckpoint(TEXT("phase6_exact_stop"));
    }
    if (bFXResourceStopping) FXResourceQuietFrames=Counts.Invalid+Counts.Pooled+Counts.Reused==132 && !Counts.PendingRetirement?FXResourceQuietFrames+1:0;
    return true;
}
bool UProject_JRenderedCrowdFixture::AdvanceFXResourceRetirement(bool& Ready,FString& Failure)
{
    check(IsInGameThread()); Ready=!FXResourceCount;
    if (!FXResourceCount) return true;
    if (bFXResourceRetired) { Ready=true; return true; }
    if (!bFXResourceStopping || !bFXResourceOverlap) { Failure=TEXT("FX retirement reached without its qualified phase5/6 lifecycle"); return false; }
    if (FXResourceQuietFrames<2) return true; // Normal native world updates only.
    RecordFXResourceCheckpoint(TEXT("native_retirement_observed"));
    CleanupFXResourceCohort();
    if (!bFXResourceCleanupPassed) { Failure=FXResourceFailure; return false; }
    FXResourceRetireSeconds=FPlatformTime::Seconds()-FXResourceStopAt; bFXResourceRetired=true; Ready=true; return true;
}
void UProject_JRenderedCrowdFixture::CleanupFXResourceCohort()
{
    check(IsInGameThread());
    if (!FXResourceCount || bFXResourceCleaned || bFXResourceCleaning) return;
    bFXResourceCleaning=true; ++FXResourceEpoch;
    if (FXResourceViewTick.IsTickFunctionRegistered()) FXResourceViewTick.UnRegisterTickFunction(); FXResourceViewTick.Owner.Reset();
    const TStrongObjectPtr<ThisClass> Self(this);
    bool Clean=true;
    for (int32 I=0;I<FXResourceRows.Num();++I)
    {
        const TStrongObjectPtr<UProject_JCombatPresentationComponent> Cue(FXResourceRows[I].Cue.Get()); const uint64 Lease=FXResourceRows[I].Lease;
        if (Cue && !Cue->IsBeingDestroyed()) { Cue->EndCueLease(Lease); Cue->EndAttackPresentation(); }
        if (Cue) Clean&=!Cue->IsCueLeaseCurrent(Lease) && !Cue->GetCueLeaseComponent(Lease).IsValid();
    }
    int32 CreatedOwners=0;
    for (int32 I=0;I<FXResourceRows.Num();++I)
    {
        if (!FXResourceRows[I].OwnerId) continue;
        ++CreatedOwners;
        const TStrongObjectPtr<AProject_JNPCCharacter> Owner(FXResourceRows[I].Owner.Get());
        if (Owner && !Owner->IsActorBeingDestroyed()) Owner->Destroy();
        if (!Owner || Owner->IsActorBeingDestroyed()) ++FXResourceDestroyedOwners; else Clean=false;
        if (auto* Cue=FXResourceRows[I].Cue.Get()) Clean&=!Cue->IsRegistered();
    }
    bFXResourceCleanupPassed=Clean && !FXResourceViewTick.IsTickFunctionRegistered() && FXResourceDestroyedOwners==CreatedOwners;
    bFXResourceCleaned=true; bFXResourceCleaning=false;
    if (!bFXResourceCleanupPassed) FXResourceFailure=TEXT("Owned FX actor/component cleanup did not complete synchronously");
    RecordFXResourceCheckpoint(TEXT("owned_resource_actors_destroyed"));
}
void UProject_JRenderedCrowdFixture::RecordFXResourceCheckpoint(const TCHAR* Label)
{
    check(IsInGameThread());
    if (!FXResourceCount || FXResourceCheckpoints.Num()>=16) return;
    auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("label"),Label); Value->SetNumberField(TEXT("phase"),Phase);
    Value->SetStringField(TEXT("frame"),LexToString(GFrameCounter)); Value->SetStringField(TEXT("observed_frame"),FXResourceView.Frame==MAX_uint64?TEXT("unobserved"):LexToString(FXResourceView.Frame));
    Value->SetNumberField(TEXT("world_time"),GetWorld()?GetWorld()->GetTimeSeconds():-1.); Value->SetNumberField(TEXT("current_leases"),FXResourceCounts.Current);
    Value->SetNumberField(TEXT("native_active"),FXResourceCounts.Active); Value->SetNumberField(TEXT("native_count_culled"),FXResourceCounts.CountCull);
    Value->SetBoolField(TEXT("system_ready_to_run"),FXResourceSystem && FXResourceSystem->IsReadyToRun());
#if WITH_EDITORONLY_DATA
    Value->SetBoolField(TEXT("editor_needs_compile_request"),FXResourceSystem && FXResourceSystem->NeedsRequestCompile());
    Value->SetBoolField(TEXT("editor_compile_outstanding"),FXResourceSystem && FXResourceSystem->HasOutstandingCompilationRequests());
#endif
    const auto Memory=FPlatformMemory::GetStats(); Value->SetNumberField(TEXT("process_used_physical_bytes"),double(Memory.UsedPhysical)); Value->SetNumberField(TEXT("process_used_virtual_bytes"),double(Memory.UsedVirtual));
    Value->SetBoolField(TEXT("rhi_texture_stats_available"),GDynamicRHI!=nullptr);
    if (GDynamicRHI)
    {
        FTextureMemoryStats T; RHIGetTextureMemoryStats(T);
        Value->SetBoolField(TEXT("rhi_hardware_stats_valid"),T.AreHardwareStatsValid());
        Value->SetNumberField(TEXT("texture_streaming_bytes"),double(T.StreamingMemorySize)); Value->SetNumberField(TEXT("texture_nonstreaming_bytes"),double(T.NonStreamingMemorySize));
        Value->SetNumberField(TEXT("texture_pool_bytes"),double(T.TexturePoolSize)); Value->SetNumberField(TEXT("hardware_dedicated_video_bytes"),double(T.DedicatedVideoMemory));
        Value->SetNumberField(TEXT("hardware_dedicated_system_bytes"),double(T.DedicatedSystemMemory)); Value->SetNumberField(TEXT("hardware_shared_system_bytes"),double(T.SharedSystemMemory)); Value->SetNumberField(TEXT("total_graphics_budget_bytes"),double(T.TotalGraphicsMemory));
    }
    if (auto* B=GetWorld()?GetWorld()->GetSubsystem<UProject_JPresentationBudgetSubsystem>():nullptr)
    { const auto& S=B->GetStats(); Value->SetStringField(TEXT("budget_stats_frame"),LexToString(S.Frame)); Value->SetBoolField(TEXT("budget_frame_is_current"),S.Frame==GFrameCounter); Value->SetStringField(TEXT("budget_applied_total"),LexToString(S.Applied)); Value->SetStringField(TEXT("budget_rejected_total"),LexToString(S.Rejected)); Value->SetNumberField(TEXT("budget_pending"),S.Pending); Value->SetNumberField(TEXT("budget_frame_attempts_raw"),S.FrameApplications); }
    TArray<TSharedPtr<FJsonValue>> Owners;
    for (int32 I=0;I<FXResourceRows.Num();++I)
    {
        const auto& R=FXResourceRows[I]; auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("slot"),I); Row->SetNumberField(TEXT("owner_id"),R.OwnerId);
        Row->SetStringField(TEXT("lease"),LexToString(R.Lease)); Row->SetNumberField(TEXT("component_id"),R.ComponentId); Row->SetBoolField(TEXT("actual_component_seen"),R.bSeen); Row->SetNumberField(TEXT("retirement"),R.Retirement);
        if (FXResourceView.Slots.IsValidIndex(I)) { const auto& S=FXResourceView.Slots[I]; Row->SetNumberField(TEXT("observed_component_id"),S.ComponentId); Row->SetNumberField(TEXT("distance"),S.Distance); Row->SetNumberField(TEXT("native_count_cull"),S.CountCull); Row->SetBoolField(TEXT("native_active"),S.bActive); }
        Owners.Add(MakeShared<FJsonValueObject>(Row));
    }
    Value->SetArrayField(TEXT("owners"),Owners); FXResourceCheckpoints.Add(Value);
}
bool UProject_JRenderedCrowdFixture::WriteFXResourceCohort(bool bSuccess,const FString& Reason)
{
    check(IsInGameThread()); if (!FXResourceCount) return true;
    if (bFXResourceWritten) return bFXResourceWriteOK; bFXResourceWritten=true;
    const bool Dirty=FXResourceSystem && FXResourceType && FXResourceSystem->GetPackage()->IsDirty()==bFXResourceSystemDirty && FXResourceType->GetPackage()->IsDirty()==bFXResourceTypeDirty;
    auto Root=MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("schema"),1); Root->SetStringField(TEXT("scope"),TEXT("standalone_mixed_cosmetic_resource_lifecycle"));
    Root->SetBoolField(TEXT("success"),bSuccess && bFXResourceRetired && bFXResourceCleanupPassed && Dirty && bFXResourceAllSeen && bFXResourceOverlap);
    Root->SetNumberField(TEXT("resource_owners"),FXResourceCount); Root->SetNumberField(TEXT("fully_animated_npcs_in_resource_cohort"),0);
    Root->SetBoolField(TEXT("retired"),bFXResourceRetired); Root->SetBoolField(TEXT("cleanup_passed"),bFXResourceCleanupPassed); Root->SetBoolField(TEXT("dirty_unchanged"),Dirty);
    Root->SetNumberField(TEXT("peak_distinct_components"),FXResourcePeakDistinct); Root->SetBoolField(TEXT("all132_actual_components"),bFXResourceAllSeen);
    Root->SetBoolField(TEXT("actual_active_observed"),bFXResourceSawActive); Root->SetBoolField(TEXT("actual_count_cull_observed"),bFXResourceSawCountCull); Root->SetBoolField(TEXT("phase5_montage_overlap"),bFXResourceOverlap);
    Root->SetNumberField(TEXT("samples"),FXResourceSamples); Root->SetNumberField(TEXT("destroyed_owned_actors"),FXResourceDestroyedOwners); Root->SetNumberField(TEXT("retirement_seconds"),FXResourceRetireSeconds);
    Root->SetStringField(TEXT("source_system"),GetPathNameSafe(FXResourceSystem.Get())); Root->SetStringField(TEXT("source_effect_type"),GetPathNameSafe(FXResourceType.Get()));
    Root->SetNumberField(TEXT("saved_effective_count_limit"),FXResourceSystem?FXResourceSystem->GetScalabilitySettings().MaxInstances:-1); Root->SetNumberField(TEXT("saved_max_pool_size"),FXResourceSystem?FXResourceSystem->MaxPoolSize:0);
    Root->SetBoolField(TEXT("remote_spawn_budget_default"),true); Root->SetNumberField(TEXT("shared_attempts_per_native_frame"),UProject_JPresentationBudgetSubsystem::MaxApplications);
    Root->SetStringField(TEXT("reason"),Reason); Root->SetStringField(TEXT("resource_failure"),FXResourceFailure);
    Root->SetStringField(TEXT("not_qualified"),TEXT("No particle/ribbon rendering, Niagara incremental memory/VRAM, mandatory warning, population capacity, full NPC132 or FPS claim. Texture/RHI/process totals include the entire mixed scene; active or recently-rendered components are not particle work. Native pool-retained/destroyed/other-owner reuse are separate. Shared128 policy is not an isolated128/4 count promise; global admission statistics include other weapons/cues and may describe an earlier frame."));
    TArray<TSharedPtr<FJsonValue>> Checkpoints; for (const auto& C:FXResourceCheckpoints) Checkpoints.Add(MakeShared<FJsonValueObject>(C)); Root->SetArrayField(TEXT("checkpoints"),Checkpoints);
    FString JSON; const auto Writer=TJsonWriterFactory<>::Create(&JSON);
    const FString JSONPath=Output/TEXT("fx-resource.json"), CSVPath=Output/TEXT("fx-resource.csv");
    bFXResourceWriteOK=!IFileManager::Get().FileExists(*JSONPath) && !IFileManager::Get().FileExists(*CSVPath)
        && FJsonSerializer::Serialize(Root,Writer) && FFileHelper::SaveStringToFile(FXResourceCSV,*CSVPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        && FFileHelper::SaveStringToFile(JSON,*JSONPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return bFXResourceWriteOK;
}
