#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "ClothingAsset.h"
#include "ClothingSimulationInteractor.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothingSimulationInteractor.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> ClothBudgetEnabled(TEXT("ProjectJ.ClothBudget.Enabled"),1,
    TEXT("Instance-only cosmetic cloth quality budget. 0 restores authored settings; local players always retain them."));

TAutoConsoleVariable<int32> ClothExecutionVertices(TEXT("ProjectJ.ClothBudget.ExecutionVertexCap"),0,
    TEXT("Opt-in cosmetic cloth geometry admission budget. 0 disables execution actuation (default); not CPU milliseconds."));

bool HasValidConfig(const UClothingAssetCommon* Asset)
{
    const auto* Cloth=Asset?Asset->GetClothConfig<UChaosClothConfig>():nullptr;
    const auto* Shared=Asset?Asset->GetClothConfig<UChaosClothSharedSimConfig>():nullptr;
    return Cloth && Shared && Shared->IterationCount>0 && Shared->MaxIterationCount>=Shared->IterationCount &&
        Shared->SubdivisionCount>0 && FMath::IsFinite(Cloth->CollisionThickness) && Cloth->CollisionThickness>=0 &&
        FMath::IsFinite(Cloth->FrictionCoefficient) && Cloth->FrictionCoefficient>=0 &&
        FMath::IsFinite(Cloth->SelfCollisionThickness) && Cloth->SelfCollisionThickness>=0;
}
}

bool UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::IsCurrent() const
{
    const auto* Component=Mesh.Get();
    if(!Component || !Component->IsRegistered() || !Asset.IsValid() || Component->GetSkeletalMeshAsset()!=Asset.Get() || Bindings.IsEmpty()) return false;
    const auto& Assets=Component->GetSkeletalMeshAsset()->GetMeshClothingAssets();
    if(Assets.Num()!=Bindings.Num()) return false;
    for(const auto& Binding:Bindings)
    {
        const auto* Cloth=Binding.Asset.Get(); const auto* Simulation=Binding.Simulation.Get();
        if(!Cloth || !Simulation || !Assets.Contains(Cloth) ||
            Component->GetClothingSimulationInteractor(Cloth->GetFName())!=Simulation ||
            !Cast<UChaosClothingInteractor>(Simulation->GetClothingInteractor(Cloth->GetFName()))) return false;
    }
    return true;
}

bool UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::Bind(UProject_JModularMeshComponent* Component)
{
    if(!Component || !Component->GetSkeletalMeshAsset()) return false;
    TArray<FClothBinding> NewBindings;
    for(const auto& Base:Component->GetSkeletalMeshAsset()->GetMeshClothingAssets())
    {
        auto* Cloth=Cast<UClothingAssetCommon>(Base);
        auto* Simulation=Cloth?Cast<UChaosClothingSimulationInteractor>(Component->GetClothingSimulationInteractor(Cloth->GetFName())):nullptr;
        if(!Simulation || !HasValidConfig(Cloth) || !Cast<UChaosClothingInteractor>(Simulation->GetClothingInteractor(Cloth->GetFName()))) return false;
        // A simulation-level iteration override cannot restore incompatible
        // authored configurations with one value. Leave that custom setup alone.
        for(const auto& Other:NewBindings) if(Other.Simulation.Get()==Simulation)
        {
            const auto* A=Cloth->GetClothConfig<UChaosClothSharedSimConfig>();
            const auto* B=Other.Asset->GetClothConfig<UChaosClothSharedSimConfig>();
            if(A->IterationCount!=B->IterationCount || A->MaxIterationCount!=B->MaxIterationCount || A->SubdivisionCount!=B->SubdivisionCount) return false;
        }
        FClothBinding Binding; Binding.Asset=Cloth; Binding.Simulation=Simulation; NewBindings.Add(Binding);
    }
    if(NewBindings.IsEmpty()) return false;
    // Immutable resource cost is captured once per binding incarnation. Include
    // fixed vertices and the largest LOD, without scanning vertex contents per frame.
    TSet<const UClothingAssetCommon*> CountedAssets;
    int64 Vertices=0; bool bKnownCost=true;
    for(const auto& Binding:NewBindings)
    {
        const auto* Clothing=Binding.Asset.Get();
        if(CountedAssets.Contains(Clothing)) continue;
        CountedAssets.Add(Clothing);
        int32 LargestLOD=0;
        for(const auto& LOD:Clothing->LodData) LargestLOD=FMath::Max(LargestLOD,LOD.PhysicalMeshData.Vertices.Num());
        if(LargestLOD<=0 || Vertices>MAX_int64-LargestLOD) { bKnownCost=false; break; }
        Vertices+=LargestLOD;
    }
    MaxLODPhysicsVertices=bKnownCost?TOptional<int64>(Vertices):TOptional<int64>();
    Mesh=Component; Asset=Component->GetSkeletalMeshAsset(); Bindings=MoveTemp(NewBindings); bReduced=false;
    return true;
}

bool UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::Apply(bool bReduce)
{
    if(!IsCurrent()) return false;
    if(bReduced==bReduce && (!bReduce || bAppliedDiscrete==Mesh->bAllowDiscreteRemoteClothCollision)) return true;
    // Validate the entire command batch before modifying any instance. Templates
    // remain read-only; interactor commands synchronize at the engine's safe point.
    for(const auto& Binding:Bindings) if(!HasValidConfig(Binding.Asset.Get())) return false;
    TSet<UClothingSimulationInteractor*> Updated;
    for(const auto& Binding:Bindings)
    {
        const auto* Cloth=Binding.Asset.Get(); auto* Simulation=Binding.Simulation.Get();
        const auto* Config=Cloth->GetClothConfig<UChaosClothConfig>();
        const auto* Shared=Cloth->GetClothConfig<UChaosClothSharedSimConfig>();
        if(!Updated.Contains(Simulation))
        {
            const int32 Iterations=bReduce?FMath::Min(2,Shared->IterationCount):Shared->IterationCount;
            const int32 Maximum=bReduce?FMath::Min(4,Shared->MaxIterationCount):Shared->MaxIterationCount;
            Simulation->SetNumIterations(Iterations);
            Simulation->SetMaxNumIterations(Maximum);
            Simulation->SetNumSubsteps(bReduce?1:Shared->SubdivisionCount);
            Updated.Add(Simulation);
        }
        auto* Interactor=CastChecked<UChaosClothingInteractor>(Simulation->GetClothingInteractor(Cloth->GetFName()));
        const bool bCCD=Config->bUseCCD && !(bReduce && Mesh->bAllowDiscreteRemoteClothCollision);
        Interactor->SetCollision(Config->CollisionThickness,Config->FrictionCoefficient,bCCD,Config->SelfCollisionThickness);
    }
    bReduced=bReduce;
    bAppliedDiscrete=bReduce && Mesh->bAllowDiscreteRemoteClothCollision;
    return true;
}

void UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::Restore(UActorComponent* Coordinator)
{
    ReleaseExecution();
    // An asset or simulation replacement owns a new configuration. Never write
    // the previous garment's defaults into that new simulation.
    if(bReduced && IsCurrent()) Apply(false);
    if(bAddedCoordinatorPrerequisite && Coordinator && Mesh.IsValid()) Mesh->RemoveTickPrerequisiteComponent(Coordinator);
    Bindings.Reset(); Mesh.Reset(); Asset.Reset(); bReduced=bAppliedDiscrete=false;
    MaxLODPhysicsVertices.Reset();
    bAddedCoordinatorPrerequisite=false;
}

void UProject_JAnimationUpdateCoordinatorComponent::DiscoverClothMesh(USkeletalMeshComponent* Mesh)
{
    auto* Part=Cast<UProject_JModularMeshComponent>(Mesh);
    if(!Part || !Part->IsRegistered() || Part->GetOwner()!=GetOwner() || !Part->bAllowClothQualityBudget ||
        !Part->LeaderPoseComponent.IsValid() || Part->ComponentTags.Contains(TEXT("NoVisualBudget")) ||
        ClothMeshes.ContainsByPredicate([Part](const FClothMesh& Entry){return Entry.Mesh.Get()==Part;})) return;
    FClothMesh Entry;
    if(Entry.Bind(Part))
    {
        Entry.bAddedCoordinatorPrerequisite=!Part->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
            [this](const FTickPrerequisite& Item){return Item.PrerequisiteObject.Get()==this;});
        if(Entry.bAddedCoordinatorPrerequisite) Part->AddTickPrerequisiteComponent(this);
        ClothMeshes.Add(MoveTemp(Entry));
    }
}

int32 UProject_JAnimationUpdateCoordinatorComponent::GetManagedClothCount() const
{ return ClothMeshes.Num(); }
int32 UProject_JAnimationUpdateCoordinatorComponent::GetReducedClothCount() const
{
    int32 Count=0;for(const auto& Entry:ClothMeshes) Count+=Entry.bReduced;
    return Count;
}
int64 UProject_JAnimationUpdateCoordinatorComponent::GetManagedClothVertexCost(int32& OutUnknownParts) const
{
    check(IsInGameThread());
    int64 Cost=0; OutUnknownParts=0;
    for(const auto& Entry:ClothMeshes)
    {
        if(!Entry.IsCurrent() || !Entry.MaxLODPhysicsVertices.IsSet() ||
            Cost>MAX_int64-Entry.MaxLODPhysicsVertices.GetValue()) { ++OutUnknownParts; continue; }
        Cost+=Entry.MaxLODPhysicsVertices.GetValue();
    }
    return Cost;
}
void UProject_JAnimationUpdateCoordinatorComponent::ObserveClothNativeFrameCompletions()
{
    check(IsInGameThread());
    if(!GetWorld() || GetWorld()->TickGroup!=TG_NewlySpawned) return;
    for(auto& Entry:ClothMeshes)
        {
        if(Entry.IsCurrent()) Entry.Mesh->ObserveClothNativeFrameCompletion();
        // Unregistering a queued native tick is not cancellation. Its two
        // copied completion events must close before this reservation is free.
        if(Entry.bExecutionOwned && Entry.Execution==EClothExecution::Parked && Entry.bExecutionReserved &&
            Entry.IsCurrent() && Entry.ExecutionLeader.Get()==Entry.Mesh->LeaderPoseComponent.Get() &&
            Entry.DrainTickCompletion.IsValid() && Entry.DrainEndCompletion.IsValid() &&
            Entry.DrainTickCompletion->IsComplete() && Entry.DrainEndCompletion->IsComplete())
        {
            Entry.DrainTickCompletion.SafeRelease(); Entry.DrainEndCompletion.SafeRelease();
            Entry.bExecutionReserved=false;
        }
    }
}
int32 UProject_JAnimationUpdateCoordinatorComponent::GetQualifiedClothNativeFrameCount(uint64 Frame) const
{
    check(IsInGameThread());
    int32 Count=0;
    for(const auto& Entry:ClothMeshes)
        if(Entry.IsCurrent() && Entry.Mesh->HasQualifiedClothNativeFrame(Frame)) ++Count;
    return Count;
}
void UProject_JAnimationUpdateCoordinatorComponent::RestoreClothBudget()
{
    for(auto& Entry:ClothMeshes) Entry.Restore(this);
    ClothMeshes.Reset(); ClothDowngradeSince=-1;
}
void UProject_JAnimationUpdateCoordinatorComponent::RefreshClothBudget(UProject_JBudgetedSkeletalMeshComponent* Source,bool bLocal,float DeltaTime)
{
    const bool bReduce=!bLocal && Source->bAllowProjectBudget && ClothBudgetEnabled.GetValueOnGameThread()!=0 && !Source->HasFullClothQualityGrant();
    const double Now=GetWorld()->GetTimeSeconds();
    if(bReduce && ClothDowngradeSince<0) ClothDowngradeSince=Now;
    if(!bReduce) ClothDowngradeSince=-1;
    for(int32 Index=ClothMeshes.Num()-1;Index>=0;--Index)
    {
        auto& Entry=ClothMeshes[Index]; auto* Mesh=Entry.Mesh.Get();
        if(!Entry.IsCurrent() || !Mesh->bAllowClothQualityBudget || !Mesh->LeaderPoseComponent.IsValid() ||
            Mesh->ComponentTags.Contains(TEXT("NoVisualBudget")))
        { Entry.Restore(this);ClothMeshes.RemoveAtSwap(Index);continue; }
        // Full quality upgrades immediately, including possession and rollback.
        // Cosmetic downgrades wait briefly without adding another timer or tick.
        if(!bReduce || Now-ClothDowngradeSince>=.2) Entry.Apply(bReduce);
    }
    RefreshClothExecution(Source,DeltaTime);
}

int64 UProject_JAnimationUpdateCoordinatorComponent::GetClothExecutionVertexCap()
{
    check(IsInGameThread());
    return FMath::Max(0,ClothExecutionVertices.GetValueOnGameThread());
}

bool UProject_JAnimationUpdateCoordinatorComponent::IsClothExecutionSource(const USkeletalMeshComponent* Source) const
{
    return Source && VisualSource.Get()==Source && Source->GetOwner()==GetOwner();
}

bool UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::CanOwnExecution() const
{
    const auto* Part=Mesh.Get();
    static const IConsoleVariable* EndWait=IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.UseEndTickWait"));
    static const IConsoleVariable* ClothEnabled=IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics"));
    static const IConsoleVariable* InTickWait=IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.WaitForParallelClothTask"));
    return IsCurrent() && !Part->IsBeingDestroyed() && IsValid(Part->GetOwner()) && !Part->GetOwner()->IsActorBeingDestroyed() &&
        Part->IsClothExecutionBudgetAllowed() && !bExecutionDeclined &&
        MaxLODPhysicsVertices.IsSet() && MaxLODPhysicsVertices.GetValue()>0 && Part->LeaderPoseComponent.IsValid() &&
        Part->LeaderPoseComponent->GetOwner()==Part->GetOwner() && !Part->IsSimulatingPhysics() &&
        Part->bAllowClothQualityBudget && !Part->ComponentTags.Contains(TEXT("NoVisualBudget")) &&
        !Part->bDisableClothSimulation && !Part->IsClothBoundToLeaderComponent() && Part->GetAllowClothActors() &&
        Part->IsComponentTickEnabled() && !Part->bWaitForParallelClothTask &&
        !Part->PrimaryComponentTick.bRunOnAnyThread && !Part->PrimaryComponentTick.bDispatchManually &&
        Part->PrimaryComponentTick.TickInterval==0 && ClothEnabled && ClothEnabled->GetInt()!=0 &&
        EndWait && EndWait->GetInt()!=0 && InTickWait && InTickWait->GetInt()==0;
}

bool UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::HasOwnedExecutionValues() const
{
    const auto* Part=Mesh.Get();
    return Part && Part->ClothBlendWeight==LastBlend && Part->IsClothingSimulationSuspended()==bLastSuspended;
}

void UProject_JAnimationUpdateCoordinatorComponent::FClothMesh::ReleaseExecution(bool bDecline)
{
    // These two controls live on the component, independently of asset/interactor
    // configuration. Restore only our unchanged last writes on that same component.
    // In particular, never Apply(old garment defaults) to a replacement simulator.
    if(bExecutionOwned)
    {
        if(auto* Part=Mesh.Get())
        {
            if(Part->ClothBlendWeight==LastBlend) Part->ClothBlendWeight=SavedBlend;
            if(Part->IsClothingSimulationSuspended()==bLastSuspended && bLastSuspended!=bSavedSuspended)
            {
                if(bSavedSuspended) Part->SuspendClothingSimulation();
                else Part->ResumeClothingSimulation();
            }
        }
    }
    bExecutionOwned=bExecutionReserved=bExecutionReleaseRequested=false;
    bExecutionDeclined|=bDecline;
    bExecutionDesired=true; bExecutionMayResume=false; bExecutionProtected=true;
    Execution=EClothExecution::Active; ExecutionAlpha=1.f;
    GrantFrame=ResumeFrame=DrainedFrame=MAX_uint64;
    ExecutionLeader.Reset(); DrainTickCompletion.SafeRelease(); DrainEndCompletion.SafeRelease();
}

void UProject_JAnimationUpdateCoordinatorComponent::ReleaseClothExecution(UProject_JModularMeshComponent* Part)
{
    check(IsInGameThread());
    for(auto& Entry:ClothMeshes) if(!Part || Entry.Mesh.Get()==Part)
    {
        Entry.ReleaseExecution();
        Entry.bExecutionDeclined=false; // Explicit exclusive-ownership handoff/rebind boundary.
    }
}

FProjectJClothExecutionDemand UProject_JAnimationUpdateCoordinatorComponent::PrepareClothExecutionDemand(bool bEnabled,bool bProtected)
{
    check(IsInGameThread());
    FProjectJClothExecutionDemand Result;
    const auto AddCost=[&Result](int64& Sum,int64 Amount)
    {
        if(Amount<=0 || Sum>MAX_int64-Amount) { Sum=MAX_int64; ++Result.Unknown; }
        else Sum+=Amount;
    };
    const bool bCanBudget=bEnabled && IsVisualBudgetEnabled() && IsComponentTickEnabled() &&
        GetWorld() && !GetWorld()->bIsTearingDown && GetNetMode()!=NM_DedicatedServer;
    for(auto& Entry:ClothMeshes)
    {
        if(!Entry.IsCurrent() || !Entry.MaxLODPhysicsVertices.IsSet())
        { Entry.bExecutionReleaseRequested|=Entry.bExecutionOwned; ++Result.Unknown; continue; }
        auto* Part=Entry.Mesh.Get();
        if(Entry.bExecutionOwned && (!Entry.HasOwnedExecutionValues() ||
            Entry.ExecutionLeader.Get()!=Part->LeaderPoseComponent.Get()))
        { Entry.bExecutionReleaseRequested=true; Entry.bExecutionDeclined=true; }
        // Policy publishes handoff requests only. Component writes belong to
        // the next coordinator PrePhysics tick or an explicit pre-mutation API.
        if(!bCanBudget || !Entry.CanOwnExecution() || bProtected)
            Entry.bExecutionReleaseRequested|=Entry.bExecutionOwned;
        const int64 Cost=Entry.MaxLODPhysicsVertices.GetValue();
        if(bCanBudget && !bProtected && !Entry.bExecutionReleaseRequested && Entry.CanOwnExecution() && !Entry.bExecutionOwned &&
            !Part->IsClothingSimulationSuspended() && FMath::IsFinite(Part->ClothBlendWeight) &&
            Part->ClothBlendWeight>0.f && Part->ClothBlendWeight<=1.f && Part->HasQualifiedClothNativeFrame(GFrameCounter))
        {
            Entry.bExecutionOwned=Entry.bExecutionReserved=true;
            Entry.SavedBlend=Entry.LastBlend=Part->ClothBlendWeight;
            Entry.bSavedSuspended=Entry.bLastSuspended=false;
            Entry.ExecutionLeader=Part->LeaderPoseComponent;
            Entry.ExecutionAlpha=1.f; Entry.Execution=EClothExecution::Active;
        }
        if(!Entry.bExecutionOwned || bProtected || Entry.bExecutionReleaseRequested)
        {
            AddCost(Result.Protected,Cost);
            continue;
        }
        AddCost(Result.Eligible,Cost);
        if(Entry.bExecutionReserved) AddCost(Result.Reserved,Cost);
        else AddCost(Result.Resume,Cost);
        Result.bPreviouslyDesired|=Entry.bExecutionDesired;
    }
    return Result;
}

void UProject_JAnimationUpdateCoordinatorComponent::SetClothExecutionGrant(bool bDesired,bool bMayResume,bool bProtected)
{
    check(IsInGameThread());
    for(auto& Entry:ClothMeshes) if(Entry.bExecutionOwned && !Entry.bExecutionReleaseRequested)
    {
        Entry.bExecutionDesired=bDesired; Entry.bExecutionMayResume=bMayResume;
        Entry.bExecutionProtected=bProtected; Entry.GrantFrame=GFrameCounter;
        // Reserve BEFORE the next primary tick may resume a parked part. A
        // subsequent policy refresh sees the outstanding reservation too.
        if(bDesired && bMayResume) Entry.bExecutionReserved=true;
    }
}

FProjectJClothExecutionStats UProject_JAnimationUpdateCoordinatorComponent::GetClothExecutionStats() const
{
    check(IsInGameThread());
    FProjectJClothExecutionStats Result;
    const auto AddCost=[&Result](int64& Sum,int64 Amount)
    {
        if(Amount<=0 || Sum>MAX_int64-Amount) { Sum=MAX_int64; ++Result.Unknown; }
        else Sum+=Amount;
    };
    for(const auto& Entry:ClothMeshes)
    {
        if(!Entry.IsCurrent() || !Entry.MaxLODPhysicsVertices.IsSet()) { ++Result.Unknown; continue; }
        const int64 Cost=Entry.MaxLODPhysicsVertices.GetValue();
        if(!Entry.bExecutionOwned)
        {
            ++Result.Unmanaged; AddCost(Result.Protected,Cost); AddCost(Result.Reserved,Cost);
            continue;
        }
        const bool bProtected=Entry.bExecutionProtected || Entry.bExecutionReleaseRequested;
        AddCost(bProtected?Result.Protected:Result.Eligible,Cost);
        if(Entry.bExecutionReserved || bProtected) AddCost(Result.Reserved,Cost);
        Result.Releasing+=Entry.bExecutionReleaseRequested;
        Result.Desired+=Entry.bExecutionDesired; Result.MayResume+=Entry.bExecutionMayResume;
        Result.Draining+=Entry.DrainTickCompletion.IsValid() || Entry.DrainEndCompletion.IsValid();
        switch(Entry.Execution)
        {
        case EClothExecution::Active: ++Result.Active; break;
        case EClothExecution::FadeOut: ++Result.FadeOut; break;
        case EClothExecution::Parked: ++Result.Parked; break;
        case EClothExecution::Warming: ++Result.Warming; break;
        case EClothExecution::FadeIn: ++Result.FadeIn; break;
        }
    }
    return Result;
}

void UProject_JAnimationUpdateCoordinatorComponent::RefreshClothExecution(UProject_JBudgetedSkeletalMeshComponent* Source,float DeltaTime)
{
    if(GetClothExecutionVertexCap()<=0)
    {
        for(auto& Entry:ClothMeshes) if(Entry.bExecutionOwned) Entry.ReleaseExecution();
        return;
    }
    const bool bEnabled=GetClothExecutionVertexCap()>0 && Source->bAllowProjectBudget;
    const bool bProtected=ProjectJ::Animation::IsClothExecutionProtected(Cast<APawn>(GetOwner())) || Source->HasFullClothQualityGrant();
    const uint64 PreviousFrame=GFrameCounter>0?GFrameCounter-1:MAX_uint64;
    const float Step=FMath::IsFinite(DeltaTime) && DeltaTime>0?FMath::Min(DeltaTime/.2f,1.f):0.f;
    for(auto& Entry:ClothMeshes)
    {
        if(!Entry.bExecutionOwned) continue;
        auto* Part=Entry.Mesh.Get();
        if(!bEnabled || bProtected || Entry.bExecutionProtected || Entry.bExecutionReleaseRequested ||
            !Entry.CanOwnExecution() || Entry.ExecutionLeader.Get()!=Part->LeaderPoseComponent.Get())
        { Entry.ReleaseExecution(); continue; }
        if(!Entry.HasOwnedExecutionValues()) { Entry.ReleaseExecution(true); continue; }
        if(Entry.GrantFrame==MAX_uint64 || GFrameCounter<Entry.GrantFrame || GFrameCounter-Entry.GrantFrame>1)
        { Entry.ReleaseExecution(true); continue; } // Lost policy owner preserves authoring.
        const bool bWanted=Entry.bExecutionDesired || Entry.bExecutionProtected;
        if(Entry.Execution==EClothExecution::Parked)
        {
            if(bWanted && Entry.bExecutionMayResume && !Entry.DrainTickCompletion.IsValid() && !Entry.DrainEndCompletion.IsValid())
            {
                Part->ResumeClothingSimulation(); Part->ForceClothNextUpdateTeleportAndReset();
                Entry.bLastSuspended=false; Entry.ResumeFrame=GFrameCounter;
                Entry.Execution=EClothExecution::Warming;
            }
            continue;
        }
        if(Entry.Execution==EClothExecution::Warming)
        {
            if(PreviousFrame!=MAX_uint64 && PreviousFrame>=Entry.ResumeFrame && Part->HasQualifiedClothNativeFrame(PreviousFrame))
                Entry.Execution=bWanted?EClothExecution::FadeIn:EClothExecution::FadeOut;
            else continue; // No elapsed-time substitute for a positive native pipeline.
        }
        if(bWanted && Entry.Execution==EClothExecution::FadeOut) Entry.Execution=EClothExecution::FadeIn;
        if(!bWanted && (Entry.Execution==EClothExecution::Active || Entry.Execution==EClothExecution::FadeIn))
            Entry.Execution=EClothExecution::FadeOut;
        if(Entry.Execution==EClothExecution::FadeIn)
        {
            Entry.ExecutionAlpha=FMath::Min(1.f,Entry.ExecutionAlpha+Step);
            if(Entry.ExecutionAlpha==1.f) Entry.Execution=EClothExecution::Active;
        }
        else if(Entry.Execution==EClothExecution::FadeOut)
            Entry.ExecutionAlpha=FMath::Max(0.f,Entry.ExecutionAlpha-Step);
        Entry.LastBlend=Entry.SavedBlend*Entry.ExecutionAlpha;
        Part->ClothBlendWeight=Entry.LastBlend;
        if(Entry.Execution==EClothExecution::FadeOut && Entry.ExecutionAlpha==0.f &&
            Part->HasQualifiedClothNativeFrame(PreviousFrame) &&
            Part->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([this](const FTickPrerequisite& Prerequisite)
                { return Prerequisite.PrerequisiteObject.Get()==this && Prerequisite.PrerequisiteTickFunction==&PrimaryComponentTick; }) &&
            Part->ClothTickFunction.IsCompletionHandleValid() && Part->ClothEndTickFunction.IsCompletionHandleValid())
        {
            const FGraphEventRef TickDone=Part->ClothTickFunction.GetCompletionHandle();
            const FGraphEventRef EndDone=Part->ClothEndTickFunction.GetCompletionHandle();
            if(TickDone.IsValid() && EndDone.IsValid() && !TickDone->IsComplete() && !EndDone->IsComplete())
            {
                Entry.DrainTickCompletion=TickDone; Entry.DrainEndCompletion=EndDone; Entry.DrainedFrame=GFrameCounter;
                Part->SuspendClothingSimulation(); Entry.bLastSuspended=true;
                Entry.Execution=EClothExecution::Parked;
                // Keep the full cost until OnWorldPostActorTick observes the copied events.
            }
        }
    }
}
