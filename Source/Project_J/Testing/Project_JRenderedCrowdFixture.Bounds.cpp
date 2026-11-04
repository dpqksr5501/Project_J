#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr int32 MaxBoundsParts=32, MaxBoundsBatches=320, MaxBoundsRows=MaxBoundsParts*MaxBoundsBatches;
constexpr double BoundsTolerance=.001;
const TCHAR* BoundsHeader=TEXT("frame,phase,phase_age,world_time,part,part_id,source,source_id,asset,source_asset,source_pa,query_sx,query_sy,query_sz,relative_identity,part_visible,part_should_render,source_visible,source_should_render,source_actor_hidden,part_bounds_scale,source_bounds_scale,old_use_leader,new_use_leader,part_all_bodies,source_all_bodies,part_include_location,source_include_location,source_fixed,source_use_leader,source_attach_bound,source_hidden_shadow,source_detail_mode,old_x,old_y,old_z,old_ex,old_ey,old_ez,old_radius,new_x,new_y,new_z,new_ex,new_ey,new_ez,new_radius,box_min_margin,sphere_margin,contained,equivalent,restored,comparison_mode,old_shared_cache,new_shared_cache,cache_restored,old_cache_hit_delta,old_cache_miss_delta,new_cache_hit_delta,new_cache_miss_delta\n");
const TCHAR* SharedCacheName=TEXT("ProjectJ.Bounds.SharedPhysicsCache");
bool ReadSharedCacheCounters(int32& Hits,int32& Misses)
{
    const auto* Hit=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsHits"));
    const auto* Miss=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsMisses"));
    if(!Hit || !Miss || !(Hit->GetFlags()&ECVF_ReadOnly) || !(Miss->GetFlags()&ECVF_ReadOnly)) return false;
    Hits=Hit->GetInt();Misses=Miss->GetInt();
    return Hits>=0 && Misses>=0 && Hits<MAX_int32 && Misses<MAX_int32;
}
FString D(double V) { return FString::Printf(TEXT("%.17g"),V); }
FString B(bool V) { return V?TEXT("1"):TEXT("0"); }
bool UniformTransform(const FTransform& T)
{
    const FVector S=T.GetScale3D();
    return !T.ContainsNaN() && T.GetRotation().IsNormalized() && S.IsUniform() &&
        FMath::Abs(S.X)>UE_SMALL_NUMBER && FMath::Abs(S.Y)>UE_SMALL_NUMBER && FMath::Abs(S.Z)>UE_SMALL_NUMBER;
}
bool BoundsFinite(const FBoxSphereBounds& V)
{
    return !V.Origin.ContainsNaN() && !V.BoxExtent.ContainsNaN() &&
        V.BoxExtent.X>=0 && V.BoxExtent.Y>=0 && V.BoxExtent.Z>=0 && FMath::IsFinite(V.SphereRadius) && V.SphereRadius>=0;
}
FString StaticBoundsFlags(const USkeletalMeshComponent& P,const USkeletalMeshComponent& S)
{
    return FString::Join(TArray<FString>{D(P.BoundsScale),D(S.BoundsScale),B(P.bUseAttachParentBound),B(P.bComponentUseFixedSkelBounds),
        B(S.bUseBoundsFromLeaderPoseComponent),B(S.bUseAttachParentBound),B(S.bComponentUseFixedSkelBounds),
        B(P.bConsiderAllBodiesForBounds),B(S.bConsiderAllBodiesForBounds),B(P.bIncludeComponentLocationIntoBounds),B(S.bIncludeComponentLocationIntoBounds),
        B(P.IsVisible()),B(S.IsVisible()),B(P.ShouldRender()),B(S.ShouldRender()),B(P.bCastHiddenShadow),B(S.bCastHiddenShadow),
        D(int32(P.DetailMode)),D(int32(S.DetailMode)),B(P.GetRelativeTransform().Equals(FTransform::Identity)),
        B(S.GetOwner() && S.GetOwner()->IsHidden())},TEXT("|"));
}
bool NativeLeader(const USkeletalMeshComponent* S)
{
    return S && (S->GetClass()==USkeletalMeshComponent::StaticClass() || S->GetClass()==UProject_JBudgetedSkeletalMeshComponent::StaticClass());
}
void AddBounds(TArray<FString>& Fields,const FBoxSphereBounds& V)
{
    Fields.Append({D(V.Origin.X),D(V.Origin.Y),D(V.Origin.Z),D(V.BoxExtent.X),D(V.BoxExtent.Y),D(V.BoxExtent.Z),D(V.SphereRadius)});
}
}

void UProject_JRenderedCrowdFixture::StartLeaderBoundsProof()
{
    check(IsInGameThread());
    BoundsProofRows=BoundsHeader;
    if(bSharedPhysicsBoundsProof)
    {
        const auto* Cache=IConsoleManager::Get().FindConsoleVariable(SharedCacheName);
        int32 Hits=0,Misses=0;
        if(!Cache || (Cache->GetFlags()&ECVF_ReadOnly) || (Cache->GetFlags()&ECVF_SetByMask)!=ECVF_SetByConstructor ||
            (Cache->GetInt()!=0 && Cache->GetInt()!=1) || !ReadSharedCacheCounters(Hits,Misses))
        { BoundsProofUnknown=TEXT("Shared cache control/counters unavailable, saturated, readonly or overridden above supported Constructor rank");return; }
        BoundsProofSharedCacheOriginal=Cache->GetInt();BoundsProofSharedCacheSetBy=Cache->GetFlags()&ECVF_SetByMask;
        bBoundsProofSharedCacheReady=true;
    }
    BoundsProofPostTick=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ThisClass::ObserveLeaderBoundsProof);
    BoundsProofCleanup=FWorldDelegates::OnWorldCleanup.AddUObject(this,&ThisClass::CleanupLeaderBoundsProof);
}
void UProject_JRenderedCrowdFixture::CleanupLeaderBoundsProof(UWorld* World,bool,bool)
{
    if(bLeaderBoundsProof && BoundsProofWorld.IsValid() && World==BoundsProofWorld.Get())
        EndLeaderBoundsProof(false,TEXT("World cleanup before normal proof completion"));
}
void UProject_JRenderedCrowdFixture::ObserveLeaderBoundsProof(UWorld* World,ELevelTick TickType,float)
{
    check(IsInGameThread());
    if(!bLeaderBoundsProof || bFinished || bBoundsProofWritten || !BoundsProofUnknown.IsEmpty() || !World || World!=GetWorld()) return;
    if(World->bIsTearingDown || TickType!=LEVELTICK_All || World->TickGroup!=TG_NewlySpawned) return;
    // GI initialization precedes the Entry map load. The startup Untitled world
    // is unrelated until an actual fixture player establishes this lifetime.
    if(!BoundsProofWorld.IsValid())
    {
        if(Players.Num()!=1 || !IsValid(Players[0].Get()) || Players[0]->GetWorld()!=World) return;
        BoundsProofWorld=World;BoundsProofPlayer=Players[0];
    }
    if(World!=BoundsProofWorld.Get() || Players.Num()!=1 || !IsValid(Players[0].Get()) ||
        Players[0].Get()!=BoundsProofPlayer.Get() || Players[0]->GetWorld()!=World)
    { ++BoundsProofIdentityInvalid;BoundsProofUnknown=TEXT("Bound fixture world/player retired or changed"); return; }
    const double Now=World->GetTimeSeconds(),Age=Now-PhaseAt;
    if(Phase<2 || Phase>6 || !FMath::IsFinite(Now) || Age<2 || Age>7 || Now-BoundsProofLastTime<.1) return;
    const TStrongObjectPtr<UProject_JRenderedCrowdFixture> KeepSelf(this);
    BoundsProofLastTime=Now;
    if(Players.Num()!=1 || !IsValid(Players[0].Get())) { BoundsProofUnknown=TEXT("Single source actor retired"); return; }
    if(!bBoundsProofSelected)
    {
        TInlineComponentArray<UProject_JModularMeshComponent*> Parts(Players[0]);
        for(auto* Part:Parts)
        {
            auto* Source=Cast<USkeletalMeshComponent>(Part->LeaderPoseComponent.Get());
            auto* Asset=Part->GetSkeletalMeshAsset();auto* SourceAsset=Source?Source->GetSkeletalMeshAsset():nullptr;
            if(Part->GetPhysicsAsset() || (Asset && Asset->HasActiveClothingAssets()))
            { BoundsProofExcluded.Add(Part->GetPathName()+TEXT(": own PA or clothing metadata; outside qualification")); continue; }
            if(BoundsProofParts.Num()>=MaxBoundsParts || Part->GetClass()!=UProject_JModularMeshComponent::StaticClass() || !NativeLeader(Source) ||
                !Asset || !SourceAsset || !IsValid(Source->GetPhysicsAsset()) || Source==Part || !Part->IsRegistered() || !Source->IsRegistered() || Part->IsBeingDestroyed() || Source->IsBeingDestroyed() ||
                Part->GetWorld()!=World || Source->GetWorld()!=World || Source->GetOwner()!=Players[0].Get() || Part->GetOwner()!=Players[0].Get() ||
                SourceAsset->HasActiveClothingAssets() || Part->bUseBoundsFromLeaderPoseComponent || Part->bUseAttachParentBound || Part->bComponentUseFixedSkelBounds ||
                Source->bUseBoundsFromLeaderPoseComponent || Source->bUseAttachParentBound || Source->bComponentUseFixedSkelBounds ||
                !FMath::IsFinite(Part->BoundsScale) || !FMath::IsFinite(Source->BoundsScale) || Part->BoundsScale<=0 || Source->BoundsScale<=0 ||
                !UniformTransform(Part->GetComponentTransform()) || !UniformTransform(Source->GetComponentTransform()))
            { BoundsProofUnknown=TEXT("Unsupported modular binding/flags/query at selection"); return; }
            FLeaderBoundsProofPart Entry;Entry.Part=Part;Entry.Leader=Source;Entry.Asset=Asset;Entry.LeaderAsset=SourceAsset;
            Entry.LeaderPA=Source->GetPhysicsAsset();Entry.StaticFlags=StaticBoundsFlags(*Part,*Source);BoundsProofParts.Add(MoveTemp(Entry));
        }
        bBoundsProofSelected=true;
        if(BoundsProofParts.IsEmpty()) { BoundsProofUnknown=TEXT("No eligible actual modular parts"); return; }
    }
    if(Players[0].Get()!=BoundsProofPlayer.Get() || BoundsProofBatches>=MaxBoundsBatches)
    { ++BoundsProofIdentityInvalid;BoundsProofUnknown=TEXT("Source identity changed or bounded sample capacity exceeded"); return; }
    ++BoundsProofBatches;
    for(int32 Index=0;Index<BoundsProofParts.Num();++Index)
    {
        const FLeaderBoundsProofPart Entry=BoundsProofParts[Index];
        const TStrongObjectPtr<UProject_JModularMeshComponent> Part(Entry.Part.Get());
        const TStrongObjectPtr<USkeletalMeshComponent> Source(Entry.Leader.Get());
        const TStrongObjectPtr<UWorld> PinWorld(World);
        const TStrongObjectPtr<AProject_JPlayerCharacter> PinActor(BoundsProofPlayer.Get());
        auto Current=[&](bool TemporaryFlag)
        {
            return !bFinished && !bBoundsProofWritten && GetWorld()==World && BoundsProofWorld.Get()==World && !World->bIsTearingDown &&
                Players.Num()==1 && Players[0].Get()==PinActor.Get() && Part.IsValid() && Source.IsValid() && PinActor.IsValid() &&
                PinActor->GetWorld()==World && !PinActor->IsActorBeingDestroyed() &&
                !Part->IsBeingDestroyed() && !Source->IsBeingDestroyed() && Part->IsRegistered() && Source->IsRegistered() &&
                Part->GetClass()==UProject_JModularMeshComponent::StaticClass() && NativeLeader(Source.Get()) &&
                Part->GetOwner()==PinActor.Get() && Source->GetOwner()==PinActor.Get() && Part->GetWorld()==World && Source->GetWorld()==World &&
                Entry.Asset.IsValid() && Entry.LeaderAsset.IsValid() && Entry.LeaderPA.IsValid() &&
                Part->LeaderPoseComponent.Get()==Source.Get() && Part->GetSkeletalMeshAsset()==Entry.Asset.Get() &&
                Source->GetSkeletalMeshAsset()==Entry.LeaderAsset.Get() && Source->GetPhysicsAsset()==Entry.LeaderPA.Get() &&
                Part->GetPhysicsAsset()==nullptr && !Entry.Asset->HasActiveClothingAssets() && !Entry.LeaderAsset->HasActiveClothingAssets() &&
                Part->bUseBoundsFromLeaderPoseComponent==TemporaryFlag && StaticBoundsFlags(*Part,*Source)==Entry.StaticFlags &&
                UniformTransform(Part->GetComponentTransform()) && UniformTransform(Source->GetComponentTransform());
        };
        if(!Current(false)) { ++BoundsProofIdentityInvalid;BoundsProofUnknown=TEXT("Frozen part/source/asset/PA/flags changed"); return; }
        if(Part->IsRunningParallelEvaluation() || Source->IsRunningParallelEvaluation()) { ++BoundsProofSkipped; continue; }
        if(BoundsProofPairs>=MaxBoundsRows) { BoundsProofUnknown=TEXT("Bounded pair output capacity exceeded"); return; }
        const FTransform Query=Part->GetComponentTransform();
        FBoxSphereBounds Old(FVector::ZeroVector,FVector::ZeroVector,0.),New=Old;
        const bool OriginalFlag=Part->bUseBoundsFromLeaderPoseComponent;
        bool PairCurrent=true,Restored=false,NewFlag=false,CacheRestored=!bSharedPhysicsBoundsProof;
        int32 OldCache=-1,NewCache=-1,OldHitDelta=0,OldMissDelta=0,NewHitDelta=0,NewMissDelta=0;
        {
            auto* Cache=bSharedPhysicsBoundsProof?IConsoleManager::Get().FindConsoleVariable(SharedCacheName):nullptr;
            const auto OriginalPriority=EConsoleVariableFlags(BoundsProofSharedCacheSetBy);
            int32 StagedCache=BoundsProofSharedCacheOriginal;
            auto CacheCurrent=[&](int32 Expected)
            {
                return !bSharedPhysicsBoundsProof || (bBoundsProofSharedCacheReady && Cache &&
                    IConsoleManager::Get().FindConsoleVariable(SharedCacheName)==Cache && !(Cache->GetFlags()&ECVF_ReadOnly) &&
                    (Cache->GetFlags()&ECVF_SetByMask)==BoundsProofSharedCacheSetBy && Cache->GetInt()==Expected);
            };
            ON_SCOPE_EXIT
            {
                if(!bSharedPhysicsBoundsProof) Part->bUseBoundsFromLeaderPoseComponent=OriginalFlag;
                if(bSharedPhysicsBoundsProof && CacheCurrent(StagedCache))
                {
                    // ReplaceCurrent excludes Constructor history even with an
                    // explicit range; use the supported explicit same-rank Set.
                    Cache->Set(BoundsProofSharedCacheOriginal,OriginalPriority);
                    CacheRestored=CacheCurrent(BoundsProofSharedCacheOriginal);
                }
                if(!bSharedPhysicsBoundsProof) Source->InvalidateCachedBounds();
                Part->InvalidateCachedBounds();
                Restored=Part->bUseBoundsFromLeaderPoseComponent==OriginalFlag && CacheRestored;
            };
            PairCurrent=CacheCurrent(BoundsProofSharedCacheOriginal);
            if(bSharedPhysicsBoundsProof && PairCurrent)
            {
                Cache->Set(0,OriginalPriority);StagedCache=0;
                PairCurrent=CacheCurrent(0);
                // Keep the source's shared entry from earlier candidate queries.
                // Dirty every selected child's native cache so baseline0 cannot
                // reuse a candidate1 native result. This is geometry proof, not FPS.
                for(const auto& ChildEntry:BoundsProofParts)
                {
                    auto* Child=ChildEntry.Part.Get();
                    if(!Child || Child->IsBeingDestroyed() || !Child->IsRegistered() || Child->GetWorld()!=World || Child->GetOwner()!=PinActor.Get())
                    { PairCurrent=false;break; }
                    Child->InvalidateCachedBounds();
                }
            }
            else if(!bSharedPhysicsBoundsProof) { Source->InvalidateCachedBounds();Part->InvalidateCachedBounds(); }
            int32 BeforeHit=0,BeforeMiss=0,AfterHit=0,AfterMiss=0;
            if(bSharedPhysicsBoundsProof) PairCurrent &= ReadSharedCacheCounters(BeforeHit,BeforeMiss);
            if(PairCurrent)
            {
                Old=Part->CalcBounds(Query);OldCache=bSharedPhysicsBoundsProof && CacheCurrent(0)?0:-1;
                PairCurrent=Current(false) && CacheCurrent(bSharedPhysicsBoundsProof?0:BoundsProofSharedCacheOriginal);
                if(bSharedPhysicsBoundsProof)
                {
                    PairCurrent &= ReadSharedCacheCounters(AfterHit,AfterMiss);
                    OldHitDelta=AfterHit-BeforeHit;OldMissDelta=AfterMiss-BeforeMiss;
                    PairCurrent &= OldHitDelta==0 && OldMissDelta==0;
                }
            }
            if(PairCurrent)
            {
                if(bSharedPhysicsBoundsProof)
                {
                    Cache->Set(1,OriginalPriority);StagedCache=1;
                    Part->InvalidateCachedBounds();
                    PairCurrent=CacheCurrent(1) && ReadSharedCacheCounters(BeforeHit,BeforeMiss);
                }
                else { Source->InvalidateCachedBounds();Part->InvalidateCachedBounds();Part->bUseBoundsFromLeaderPoseComponent=true; }
                if(PairCurrent)
                {
                    New=Part->CalcBounds(Query);NewFlag=Part->bUseBoundsFromLeaderPoseComponent;
                    NewCache=bSharedPhysicsBoundsProof && CacheCurrent(1)?1:-1;
                    PairCurrent=Current(!bSharedPhysicsBoundsProof) && Part->GetComponentTransform().Equals(Query) &&
                        CacheCurrent(bSharedPhysicsBoundsProof?1:BoundsProofSharedCacheOriginal);
                    if(bSharedPhysicsBoundsProof)
                    {
                        PairCurrent &= ReadSharedCacheCounters(AfterHit,AfterMiss);
                        NewHitDelta=AfterHit-BeforeHit;NewMissDelta=AfterMiss-BeforeMiss;
                        PairCurrent &= NewHitDelta>=0 && NewHitDelta<=1 && NewMissDelta>=0 && NewMissDelta<=1 && NewHitDelta+NewMissDelta==1;
                    }
                }
            }
        }
        if(!Restored) ++BoundsProofRestoreFailures;
        if(!PairCurrent || !Current(false)) { ++BoundsProofIdentityInvalid;BoundsProofUnknown=TEXT("Native pair lost its frozen binding/control or cache hit/miss evidence"); return; }
        if(!BoundsFinite(Old) || !BoundsFinite(New)) { BoundsProofUnknown=TEXT("Native pair returned nonfinite/invalid bounds"); return; }
        const FVector OldMin=Old.Origin-Old.BoxExtent,OldMax=Old.Origin+Old.BoxExtent,NewMin=New.Origin-New.BoxExtent,NewMax=New.Origin+New.BoxExtent;
        const FVector Low=OldMin-NewMin,High=NewMax-OldMax;
        const double BoxMargin=FMath::Min3(FMath::Min(Low.X,High.X),FMath::Min(Low.Y,High.Y),FMath::Min(Low.Z,High.Z));
        const double SphereMargin=New.SphereRadius-Old.SphereRadius-FVector::Distance(New.Origin,Old.Origin);
        if(Low.ContainsNaN() || High.ContainsNaN() || !FMath::IsFinite(BoxMargin) || !FMath::IsFinite(SphereMargin))
        { BoundsProofUnknown=TEXT("Native envelope margins were nonfinite"); return; }
        const bool Contained=BoxMargin>=-BoundsTolerance && SphereMargin>=-BoundsTolerance;
        const bool Equivalent=Old.Origin.Equals(New.Origin,BoundsTolerance) && Old.BoxExtent.Equals(New.BoxExtent,BoundsTolerance) && FMath::Abs(New.SphereRadius-Old.SphereRadius)<=BoundsTolerance;
        if(!Contained) ++BoundsProofViolations;
        ++BoundsProofPairs;++BoundsProofParts[Index].PhasePairs[Phase];
        if(Equivalent) ++BoundsProofEquivalentPairs;
        BoundsProofCacheHitPairs+=NewHitDelta;BoundsProofCacheMissPairs+=NewMissDelta;
        BoundsProofMaxBoxDelta=FMath::Max(BoundsProofMaxBoxDelta,FMath::Max((NewMin-OldMin).GetAbsMax(),(NewMax-OldMax).GetAbsMax()));
        BoundsProofMaxRadiusDelta=FMath::Max(BoundsProofMaxRadiusDelta,FMath::Abs(New.SphereRadius-Old.SphereRadius));
        const FVector Scale=Query.GetScale3D();
        TArray<FString> Fields={LexToString(GFrameCounter),LexToString(Phase),D(Age),D(Now),Part->GetPathName(),LexToString(Part->GetUniqueID()),Source->GetPathName(),LexToString(Source->GetUniqueID()),
            GetPathNameSafe(Entry.Asset.Get()),GetPathNameSafe(Entry.LeaderAsset.Get()),GetPathNameSafe(Entry.LeaderPA.Get()),D(Scale.X),D(Scale.Y),D(Scale.Z),B(Part->GetRelativeTransform().Equals(FTransform::Identity)),
            B(Part->IsVisible()),B(Part->ShouldRender()),B(Source->IsVisible()),B(Source->ShouldRender()),B(Source->GetOwner()->IsHidden()),D(Part->BoundsScale),D(Source->BoundsScale),B(OriginalFlag),B(NewFlag),
            B(Part->bConsiderAllBodiesForBounds),B(Source->bConsiderAllBodiesForBounds),B(Part->bIncludeComponentLocationIntoBounds),B(Source->bIncludeComponentLocationIntoBounds),B(Source->bComponentUseFixedSkelBounds),
            B(Source->bUseBoundsFromLeaderPoseComponent),B(Source->bUseAttachParentBound),B(Source->bCastHiddenShadow),LexToString(int32(Source->DetailMode))};
        AddBounds(Fields,Old);AddBounds(Fields,New);
        Fields.Append({D(BoxMargin),D(SphereMargin),B(Contained),B(Equivalent),B(Restored)});
        Fields.Append({bSharedPhysicsBoundsProof?TEXT("shared_physics_cache"):TEXT("native_leader_bounds"),LexToString(OldCache),LexToString(NewCache),B(CacheRestored),
            LexToString(OldHitDelta),LexToString(OldMissDelta),LexToString(NewHitDelta),LexToString(NewMissDelta)});
        BoundsProofRows+=FString::Join(Fields,TEXT(","))+TEXT("\n");
    }
}

bool UProject_JRenderedCrowdFixture::EndLeaderBoundsProof(bool RunCompleted,const FString& Reason)
{
    check(IsInGameThread());
    FWorldDelegates::OnWorldPostActorTick.Remove(BoundsProofPostTick);BoundsProofPostTick.Reset();
    FWorldDelegates::OnWorldCleanup.Remove(BoundsProofCleanup);BoundsProofCleanup.Reset();
    if(bBoundsProofWritten) return bBoundsProofQualified;
    bBoundsProofWritten=true;
    bool Covered=bBoundsProofSelected && !BoundsProofParts.IsEmpty(),OriginalFlagsCurrent=true;
    TArray<TSharedPtr<FJsonValue>> Parts;
    for(const auto& Entry:BoundsProofParts)
    {
        const auto* Part=Entry.Part.Get();const auto* Source=Entry.Leader.Get();
        const bool FinalCurrent=Part && Source && Entry.Asset.IsValid() && Entry.LeaderAsset.IsValid() && Entry.LeaderPA.IsValid() &&
            !Part->IsBeingDestroyed() && !Source->IsBeingDestroyed() && Part->IsRegistered() && Source->IsRegistered() &&
            Part->GetWorld()==BoundsProofWorld.Get() && Source->GetWorld()==BoundsProofWorld.Get() &&
            Part->GetOwner()==BoundsProofPlayer.Get() && Source->GetOwner()==BoundsProofPlayer.Get() &&
            Part->LeaderPoseComponent.Get()==Source && Part->GetSkeletalMeshAsset()==Entry.Asset.Get() &&
            Source->GetSkeletalMeshAsset()==Entry.LeaderAsset.Get() && Source->GetPhysicsAsset()==Entry.LeaderPA.Get() &&
            Part->GetPhysicsAsset()==nullptr && !Entry.Asset->HasActiveClothingAssets() && !Entry.LeaderAsset->HasActiveClothingAssets() &&
            !Part->bUseBoundsFromLeaderPoseComponent && StaticBoundsFlags(*Part,*Source)==Entry.StaticFlags;
        OriginalFlagsCurrent &= Part && !Part->bUseBoundsFromLeaderPoseComponent;
        if(!FinalCurrent) { ++BoundsProofIdentityInvalid; if(BoundsProofUnknown.IsEmpty()) BoundsProofUnknown=TEXT("Frozen identity/flags changed before proof end"); }
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("part"),GetPathNameSafe(Entry.Part.Get()));Row->SetStringField(TEXT("source"),GetPathNameSafe(Entry.Leader.Get()));
        Row->SetStringField(TEXT("part_class"),GetPathNameSafe(Part?Part->GetClass():nullptr));Row->SetStringField(TEXT("source_class"),GetPathNameSafe(Source?Source->GetClass():nullptr));
        Row->SetStringField(TEXT("asset"),GetPathNameSafe(Entry.Asset.Get()));Row->SetStringField(TEXT("source_asset"),GetPathNameSafe(Entry.LeaderAsset.Get()));Row->SetStringField(TEXT("source_pa"),GetPathNameSafe(Entry.LeaderPA.Get()));
        auto Coverage=MakeShared<FJsonObject>();
        for(int32 P=2;P<=6;++P) { Coverage->SetNumberField(LexToString(P),Entry.PhasePairs[P]);Covered &= Entry.PhasePairs[P]>0; }
        Row->SetObjectField(TEXT("phase_pairs"),Coverage);Parts.Add(MakeShared<FJsonValueObject>(Row));
    }
    const auto* FinalCache=bSharedPhysicsBoundsProof?IConsoleManager::Get().FindConsoleVariable(SharedCacheName):nullptr;
    const bool CacheRestored=!bSharedPhysicsBoundsProof || (bBoundsProofSharedCacheReady && FinalCache && !(FinalCache->GetFlags()&ECVF_ReadOnly) &&
        (FinalCache->GetFlags()&ECVF_SetByMask)==BoundsProofSharedCacheSetBy && FinalCache->GetInt()==BoundsProofSharedCacheOriginal);
    bBoundsProofQualified=RunCompleted && Covered && BoundsProofUnknown.IsEmpty() && BoundsProofViolations==0 && BoundsProofIdentityInvalid==0 && BoundsProofRestoreFailures==0 && BoundsProofPairs>0 &&
        CacheRestored && (!bSharedPhysicsBoundsProof || (BoundsProofEquivalentPairs==BoundsProofPairs && BoundsProofCacheHitPairs>0 && BoundsProofCacheHitPairs+BoundsProofCacheMissPairs==BoundsProofPairs));
    auto Receipt=MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("comparison_mode"),bSharedPhysicsBoundsProof?TEXT("shared_physics_cache"):TEXT("native_leader_bounds"));
    Receipt->SetNumberField(TEXT("csv_columns"),60);
    Receipt->SetStringField(TEXT("shared_cache_control"),bSharedPhysicsBoundsProof?SharedCacheName:TEXT("not_used"));
    Receipt->SetNumberField(TEXT("shared_cache_original_value"),bBoundsProofSharedCacheReady?BoundsProofSharedCacheOriginal:-1);
    Receipt->SetNumberField(TEXT("shared_cache_original_set_by"),bBoundsProofSharedCacheReady?double(BoundsProofSharedCacheSetBy):-1.);
    Receipt->SetBoolField(TEXT("shared_cache_restored"),CacheRestored);
    Receipt->SetNumberField(TEXT("cache_hit_pairs"),BoundsProofCacheHitPairs);Receipt->SetNumberField(TEXT("cache_miss_pairs"),BoundsProofCacheMissPairs);
    Receipt->SetStringField(TEXT("scope"),TEXT("One actor; sampled original native final Box+Sphere envelope non-shrink, not all content vertices or automatic authoring/resource approval"));
    Receipt->SetBoolField(TEXT("run_completed"),RunCompleted);Receipt->SetBoolField(TEXT("unknown"),!BoundsProofUnknown.IsEmpty());
    Receipt->SetStringField(TEXT("unknown_reason"),BoundsProofUnknown);Receipt->SetStringField(TEXT("end_reason"),Reason);
    Receipt->SetBoolField(TEXT("original_flags_restored"),BoundsProofRestoreFailures==0 && OriginalFlagsCurrent);Receipt->SetBoolField(TEXT("all_phase_coverage"),Covered);
    Receipt->SetNumberField(TEXT("pairs"),BoundsProofPairs);Receipt->SetNumberField(TEXT("batches"),BoundsProofBatches);Receipt->SetNumberField(TEXT("skipped_parallel"),BoundsProofSkipped);
    Receipt->SetNumberField(TEXT("identity_invalid"),BoundsProofIdentityInvalid);Receipt->SetNumberField(TEXT("violations"),BoundsProofViolations);Receipt->SetNumberField(TEXT("tolerance_cm"),BoundsTolerance);
    Receipt->SetNumberField(TEXT("equivalent_pairs"),BoundsProofEquivalentPairs);Receipt->SetNumberField(TEXT("max_box_coordinate_delta_cm"),BoundsProofMaxBoxDelta);Receipt->SetNumberField(TEXT("max_radius_delta_cm"),BoundsProofMaxRadiusDelta);
    Receipt->SetArrayField(TEXT("parts"),Parts);TArray<TSharedPtr<FJsonValue>> Excluded;for(const auto& V:BoundsProofExcluded) Excluded.Add(MakeShared<FJsonValueString>(V));Receipt->SetArrayField(TEXT("excluded"),Excluded);
    const bool CSV=FFileHelper::SaveStringToFile(BoundsProofRows.IsEmpty()?FString(BoundsHeader):BoundsProofRows,*(Output/TEXT("bounds_native_pairs.csv")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting);
    bBoundsProofQualified &= CSV;
    Receipt->SetBoolField(TEXT("qualified"),bBoundsProofQualified);Receipt->SetBoolField(TEXT("csv_saved"),CSV);
    FString JSON;const auto Writer=TJsonWriterFactory<>::Create(&JSON);const bool Encoded=FJsonSerializer::Serialize(Receipt,Writer);
    const bool Saved=Encoded && FFileHelper::SaveStringToFile(JSON,*(Output/TEXT("bounds_native_proof.json")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting);
    bBoundsProofQualified &= Saved;
    return bBoundsProofQualified;
}
