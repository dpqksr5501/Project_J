#include "Components/Project_JModularMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Animation/Project_JCopyPoseAnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "ClothingSimulationInstance.h"
#include "ClothingSimulationInteractor.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Templates/UniquePtr.h"
#include "UnrealEngine.h"
#include <atomic>

namespace
{
TAutoConsoleVariable<int32> CVarSharedPhysicsBounds(TEXT("ProjectJ.Bounds.SharedPhysicsCache"),0,
    TEXT("Opt-in exact native local physics-bounds reuse across eligible shared-pose parts. 0: original native path (default). No quality, pose or physics change."));
using FSharedBodyValues=TArray<Project_J::Bounds::FBodyValue,TInlineAllocator<Project_J::Bounds::MaxSharedBodies>>;
#if !UE_BUILD_SHIPPING
std::atomic<int32> SharedPhysicsHits{0}, SharedPhysicsMisses{0};
struct FDelegatedCounterDeleter
{
    void operator()(IConsoleVariable* Variable) const
    {
        IConsoleManager::Get().UnregisterConsoleObject(Variable,false);
    }
};
// Delegated readonly reads avoid CVar Set/string/history work in the hot path.
// Only GT increments; other threads may observe these relaxed diagnostic values.
// Register directly: delegated variables expose GetInt, not typed variable data.
// Remove the object/delegates before module code and the earlier atomics retire.
TUniquePtr<IConsoleVariable,FDelegatedCounterDeleter> CVarSharedPhysicsHits(IConsoleManager::Get().RegisterDelegatedConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsHits"),int32(0),
    [] { return SharedPhysicsHits.load(std::memory_order_relaxed); },[](const int32&) {},
    TEXT("Read-only saturated count of actual opt-in native shared-local-bounds installations."),ECVF_ReadOnly));
TUniquePtr<IConsoleVariable,FDelegatedCounterDeleter> CVarSharedPhysicsMisses(IConsoleManager::Get().RegisterDelegatedConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsMisses"),int32(0),
    [] { return SharedPhysicsMisses.load(std::memory_order_relaxed); },[](const int32&) {},
    TEXT("Read-only saturated count of eligible shared-bounds misses that invoke native Super calculation."),ECVF_ReadOnly));
#endif


bool IsFiniteUniformQuery(const FTransform& T)
{
    const FVector Scale=T.GetScale3D();
    return !T.ContainsNaN() && T.GetRotation().IsNormalized() && Scale.IsUniform() &&
        FMath::Abs(Scale.X)>UE_SMALL_NUMBER && FMath::Abs(Scale.Y)>UE_SMALL_NUMBER && FMath::Abs(Scale.Z)>UE_SMALL_NUMBER;
}
bool IsCleanBoundsAsset(const UObject* Asset)
{
    return IsValid(Asset) && !Asset->GetOutermost()->IsDirty();
}
bool IsFiniteLocalBounds(const FBoxSphereBounds& V)
{
    return !V.Origin.ContainsNaN() && !V.BoxExtent.ContainsNaN() && V.BoxExtent.X>=0 && V.BoxExtent.Y>=0 && V.BoxExtent.Z>=0 &&
        FMath::IsFinite(V.SphereRadius) && V.SphereRadius>=0;
}
// This is the original native PA body selection/getter sequence, not an AABB
// solver. Local-cache mode calls CalcMeshBound with Identity, not caller world T.
bool CaptureSharedPhysicsBounds(const UProject_JModularMeshComponent& Part,const UProject_JBudgetedSkeletalMeshComponent& Source,
    const FTransform& Query,Project_J::Bounds::FKey& Key,FSharedBodyValues& Values)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Bounds_SharedKey);
    static const IConsoleVariable* CacheLocal=nullptr;
    if(!CacheLocal) CacheLocal=IConsoleManager::Get().FindConsoleVariable(TEXT("a.CacheLocalSpaceBounds"));
    if(!CacheLocal || CacheLocal->GetInt()!=1 || Part.GetClass()!=UProject_JModularMeshComponent::StaticClass() ||
        Source.GetClass()!=UProject_JBudgetedSkeletalMeshComponent::StaticClass()) return false;
    const auto* World=Part.GetWorld();const auto* Owner=Part.GetOwner();
    const auto* Asset=Part.GetSkeletalMeshAsset();const auto* SourceAsset=Source.GetSkeletalMeshAsset();
    const auto* PA=Source.GetPhysicsAsset();
    if(!IsValid(World) || World->bIsTearingDown || (World->WorldType!=EWorldType::Game && World->WorldType!=EWorldType::PIE) ||
        !IsValid(Owner) || Owner->IsActorBeingDestroyed() || Part.IsTemplate() || Source.IsTemplate() ||
        !Part.IsRegistered() || !Source.IsRegistered() || Part.IsBeingDestroyed() || Source.IsBeingDestroyed() ||
        Source.GetWorld()!=World || Source.GetOwner()!=Owner || Part.LeaderPoseComponent.Get()!=&Source || Source.LeaderPoseComponent.IsValid() ||
        Part.GetPhysicsAsset()!=nullptr || !IsCleanBoundsAsset(PA) || PA->GetClass()!=UPhysicsAsset::StaticClass() || PA->SkeletalBodySetups.IsEmpty() ||
        !IsCleanBoundsAsset(Asset) || !IsCleanBoundsAsset(SourceAsset) || Asset->GetClass()!=USkeletalMesh::StaticClass() || SourceAsset->GetClass()!=USkeletalMesh::StaticClass() ||
        Part.bUseBoundsFromLeaderPoseComponent || Part.bUseAttachParentBound || Part.bComponentUseFixedSkelBounds || Part.bIncludeComponentLocationIntoBounds ||
        Source.bUseBoundsFromLeaderPoseComponent || Source.bUseAttachParentBound || Source.bComponentUseFixedSkelBounds || Source.bIncludeComponentLocationIntoBounds ||
        !FMath::IsFinite(Part.BoundsScale) || !FMath::IsFinite(Source.BoundsScale) || Part.BoundsScale<=0 || Source.BoundsScale<=0 ||
        !Part.ShouldRender() || Part.DetailMode>GetCachedScalabilityCVars().DetailMode ||
        Part.IsRunningParallelEvaluation() || Source.IsRunningParallelEvaluation() || Part.bBlendPhysics || Source.bBlendPhysics ||
        Part.IsAnySimulatingPhysics() || Source.IsAnySimulatingPhysics() || !IsFiniteUniformQuery(Query) || !IsFiniteUniformQuery(Source.GetComponentTransform())) return false;
#if WITH_EDITOR
    if(Part.IsCompiling() || Source.IsCompiling() || Asset->IsCompiling() || SourceAsset->IsCompiling()) return false;
#endif
    if(Asset->HasActiveClothingAssets() || SourceAsset->HasActiveClothingAssets() ||
        !Part.GetClothingSimulationInstances().IsEmpty() || !Source.GetClothingSimulationInstances().IsEmpty()) return false;
    const int32 Count=Part.bConsiderAllBodiesForBounds?PA->SkeletalBodySetups.Num():PA->BoundsBodies.Num();
    if(Count<0 || Count>Project_J::Bounds::MaxSharedBodies) return false;
    Key.Frame=GFrameCounter;Key.Source=FObjectKey(&Source);Key.Owner=FObjectKey(Owner);Key.World=FObjectKey(World);
    Key.SourceAsset=FObjectKey(SourceAsset);Key.PhysicsAsset=FObjectKey(PA);Key.BoundsScale=Part.BoundsScale;Key.SourceBoundsScale=Source.BoundsScale;
    Key.NativeFlags=uint32(Part.bConsiderAllBodiesForBounds) | (uint32(Part.bCastHiddenShadow)<<1) |
        (uint32(Source.ShouldRender())<<2) | (uint32(Source.bCastHiddenShadow)<<3) | (uint32(Source.bConsiderAllBodiesForBounds)<<4);
    Key.DetailMode=GetCachedScalabilityCVars().DetailMode;
    Values.Reset();
    const auto& LeaderMap=Part.GetLeaderBoneMap();
    for(int32 I=0;I<Count;++I)
    {
        const int32 BodyIndex=Part.bConsiderAllBodiesForBounds?I:PA->BoundsBodies[I];
        if(!PA->SkeletalBodySetups.IsValidIndex(BodyIndex)) return false;
        const auto* Body=PA->SkeletalBodySetups[BodyIndex].Get();
        if(!IsCleanBoundsAsset(Body) || Body->GetClass()!=USkeletalBodySetup::StaticClass() || !Body->BodySetupGuid.IsValid()) return false;
        Project_J::Bounds::FBodyValue V;V.Body=FObjectKey(Body);V.GeometryGuid=Body->BodySetupGuid;V.BoneName=Body->BoneName;
        V.bConsider=Body->bConsiderForBounds || Part.bConsiderAllBodiesForBounds;
        if(V.bConsider)
        {
            const int32 BoneIndex=Part.GetBoneIndex(Body->BoneName);V.bPresent=BoneIndex!=INDEX_NONE;
            if(V.bPresent)
            {
                // Admit canonical named leader mappings only; never reproduce the
                // missing-bone ancestor fallback. Bone index itself is not the key.
                const int32 SourceIndex=Source.GetBoneIndex(Body->BoneName);
                if(!LeaderMap.IsValidIndex(BoneIndex) || SourceIndex<0 || SourceIndex>=Source.GetNumComponentSpaceTransforms() || LeaderMap[BoneIndex]!=SourceIndex) return false;
                V.Transform=Part.GetBoneTransform(BoneIndex,FTransform::Identity);
                if(V.Transform.ContainsNaN() || !V.Transform.GetRotation().IsNormalized()) return false;
            }
        }
        Values.Add(MoveTemp(V));
    }
    return true;
}

TAutoConsoleVariable<int32> CVarObserveClothExecution(TEXT("ProjectJ.ClothBudget.ObserveExecution"), 0,
    TEXT("Observe eligible native cloth tick/end-tick completion without changing execution. 0: off (default)."));
}

FBoxSphereBounds UProject_JModularMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    // Original native cache hits, including non-GT callers, never build a key.
    if(bCachedLocalBoundsUpToDate || bCachedWorldSpaceBoundsUpToDate) return Super::CalcBounds(LocalToWorld);
    if(!IsInGameThread() || IsGarbageCollecting() || CVarSharedPhysicsBounds.GetValueOnGameThread()<=0) return Super::CalcBounds(LocalToWorld);
    const auto* Source=Cast<UProject_JBudgetedSkeletalMeshComponent>(LeaderPoseComponent.Get());
    if(!Source || !Source->IsSharedPhysicsBoundsAvailable()) return Super::CalcBounds(LocalToWorld);
    // Child identity protects this query but is not shared semantic equality:
    // sibling meshes may differ while their actual selected body values agree.
    const auto* ChildAsset=GetSkeletalMeshAsset();
    Project_J::Bounds::FKey Key;FSharedBodyValues Values;
    if(!CaptureSharedPhysicsBounds(*this,*Source,LocalToWorld,Key,Values) || GetSkeletalMeshAsset()!=ChildAsset)
        return Super::CalcBounds(LocalToWorld);
    FBoxSphereBounds Shared(FVector::ZeroVector,FVector::ZeroVector,0.);
    if(Source->TryReadSharedPhysicsBounds(Key,Values,Shared) && GetSkeletalMeshAsset()==ChildAsset && IsFiniteLocalBounds(Shared))
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Bounds_SharedHit);
        CachedWorldOrLocalSpaceBounds=Shared;CachedWorldToLocalTransform.SetIdentity();
        bCachedLocalBoundsUpToDate=true;bCachedWorldSpaceBoundsUpToDate=false;
#if !UE_BUILD_SHIPPING
        const int32 HitCount=SharedPhysicsHits.load(std::memory_order_relaxed);
        if(HitCount<MAX_int32) SharedPhysicsHits.store(HitCount+1,std::memory_order_relaxed);
#endif
        return Super::CalcBounds(LocalToWorld); // Native conversion with THIS caller's exact world query.
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Bounds_SharedMiss);
    const TStrongObjectPtr<const UProject_JModularMeshComponent> KeepSelf(this);
    const TStrongObjectPtr<const UProject_JBudgetedSkeletalMeshComponent> KeepSource(Source);
#if !UE_BUILD_SHIPPING
    const int32 MissCount=SharedPhysicsMisses.load(std::memory_order_relaxed);
    if(MissCount<MAX_int32) SharedPhysicsMisses.store(MissCount+1,std::memory_order_relaxed);
#endif
    const FBoxSphereBounds Result=Super::CalcBounds(LocalToWorld);
    if(GetSkeletalMeshAsset()!=ChildAsset || !Source->IsSharedPhysicsBoundsAvailable() ||
        !bCachedLocalBoundsUpToDate || bCachedWorldSpaceBoundsUpToDate || !CachedWorldToLocalTransform.Equals(FMatrix::Identity,0.) ||
        !IsFiniteLocalBounds(CachedWorldOrLocalSpaceBounds)) return Result;
    Project_J::Bounds::FKey AfterKey;FSharedBodyValues AfterValues;
    if(CaptureSharedPhysicsBounds(*this,*Source,LocalToWorld,AfterKey,AfterValues) && Key==AfterKey && Project_J::Bounds::SameBodies(Values,AfterValues))
        Source->PublishSharedPhysicsBounds(Key,Values,CachedWorldOrLocalSpaceBounds);
    return Result;
}

bool UProject_JModularMeshComponent::IsClothExecutionObservationEnabled()
{
    check(IsInGameThread());
    return CVarObserveClothExecution.GetValueOnGameThread() != 0 ||
        UProject_JAnimationUpdateCoordinatorComponent::GetClothExecutionVertexCap() > 0;
}

void UProject_JModularMeshComponent::ResetClothNativeObservation()
{
    ClothObservationArmedFrame = ClothObservationQualifiedFrame = MAX_uint64;
    ClothObservationArmedRegistration = 0; ClothObservationLOD = INDEX_NONE;
    ClothObservationAsset.Reset(); ClothObservationLeader.Reset(); ClothObservationInteractors.Reset();
    ClothObservationTickCompletion.SafeRelease(); ClothObservationEndCompletion.SafeRelease();
}

bool UProject_JModularMeshComponent::IsClothNativeObservationEligible() const
{
    const UWorld* ClothWorld = GetWorld();
    const AActor* ClothOwner = GetOwner();
    const USkeletalMesh* ClothAsset = GetSkeletalMeshAsset();
    if (!IsRegistered() || IsBeingDestroyed() || IsTemplate() || !ClothWorld || ClothWorld->bIsTearingDown
        || (ClothWorld->WorldType != EWorldType::Game && ClothWorld->WorldType != EWorldType::PIE)
        || ClothWorld->IsPaused() || !FMath::IsFinite(ClothWorld->GetDeltaSeconds()) || ClothWorld->GetDeltaSeconds() <= 0
        || !IsValid(ClothOwner) || ClothOwner->IsActorBeingDestroyed() || !FMath::IsFinite(ClothOwner->CustomTimeDilation)
        || ClothOwner->CustomTimeDilation <= 0 || !FApp::CanEverRender() || !IsValid(ClothAsset)
        || !bRecentlyRendered || bDisableClothSimulation || IsClothingSimulationSuspended()
        || IsClothBoundToLeaderComponent() || bWaitForParallelClothTask || !CanSimulateClothing()
        || !ClothAsset->HasActiveClothingAssetsForLOD(GetPredictedLODLevel())) return false;
#if WITH_EDITOR
    if (ClothAsset->IsCompiling()) return false;
#endif
    // Match the default native end-tick path; the EOF fallback is unconfirmed.
    static const IConsoleVariable* EndWait = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.UseEndTickWait"));
    static const IConsoleVariable* InTickWait = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClothPhysics.WaitForParallelClothTask"));
    if (!EndWait || EndWait->GetInt() == 0 || !InTickWait || InTickWait->GetInt() != 0) return false;
    const auto NormalTick = [](const FTickFunction& NativeTick)
    {
        return NativeTick.IsTickFunctionRegistered() && NativeTick.IsTickFunctionEnabled() && NativeTick.TickInterval == 0
            && !NativeTick.bRunOnAnyThread && !NativeTick.bDispatchManually;
    };
    if (!NormalTick(PrimaryComponentTick) || !NormalTick(ClothTickFunction) || !NormalTick(ClothEndTickFunction)
        || ClothTickFunction.Target != this || ClothEndTickFunction.Target != this
        || ClothTickFunction.TickGroup != TG_PrePhysics || ClothTickFunction.EndTickGroup != TG_PostPhysics
        || ClothTickFunction.GetActualTickGroup() > TG_PostPhysics || ClothTickFunction.GetActualEndTickGroup() > TG_PostPhysics
        || ClothEndTickFunction.TickGroup != TG_LastDemotable || ClothEndTickFunction.EndTickGroup != TG_LastDemotable
        || ClothEndTickFunction.GetActualTickGroup() != TG_LastDemotable || ClothEndTickFunction.GetActualEndTickGroup() != TG_LastDemotable) return false;
    if (!ClothTickFunction.GetPrerequisites().ContainsByPredicate([this](const FTickPrerequisite& NativePrerequisite)
        { return NativePrerequisite.PrerequisiteObject.Get() == this && NativePrerequisite.PrerequisiteTickFunction == &PrimaryComponentTick; })
        || !ClothEndTickFunction.GetPrerequisites().ContainsByPredicate([this](const FTickPrerequisite& NativePrerequisite)
        { return NativePrerequisite.PrerequisiteObject.Get() == this && NativePrerequisite.PrerequisiteTickFunction == &ClothTickFunction; })) return false;
    const auto& NativeSimulations = GetClothingSimulationInstances();
    if (NativeSimulations.IsEmpty()) return false;
    for (const FClothingSimulationInstance& NativeSimulation : NativeSimulations)
        if (!IsValid(NativeSimulation.GetClothingSimulationInteractor())) return false;
    return true;
}

bool UProject_JModularMeshComponent::IsClothNativeObservationCurrent() const
{
    if (!IsClothNativeObservationEligible() || ClothObservationArmedRegistration != ClothObservationRegistration
        || !ClothObservationAsset.IsValid() || ClothObservationAsset.Get() != GetSkeletalMeshAsset()
        || ClothObservationLeader.IsStale() || ClothObservationLeader.Get() != LeaderPoseComponent.Get()
        || ClothObservationLOD != GetPredictedLODLevel()) return false;
    const auto& NativeSimulations = GetClothingSimulationInstances();
    if (NativeSimulations.Num() != ClothObservationInteractors.Num()) return false;
    for (int32 NativeIndex = 0; NativeIndex < NativeSimulations.Num(); ++NativeIndex)
        if (!ClothObservationInteractors[NativeIndex].IsValid()
            || ClothObservationInteractors[NativeIndex].Get() != NativeSimulations[NativeIndex].GetClothingSimulationInteractor()) return false;
    return true;
}

void UProject_JModularMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    // The observer supports GT ticks only; an authored concurrent tick keeps
    // its normal engine behavior and is never treated as completion evidence.
    if (!IsInGameThread()) return;
    ResetClothNativeObservation();
    if (!IsClothExecutionObservationEnabled() || TickType != LEVELTICK_All || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0
        || !IsClothNativeObservationEligible() || !ClothTickFunction.IsCompletionHandleValid()
        || !ClothEndTickFunction.IsCompletionHandleValid()) return;
    // The primary tick has not returned yet, so its native dependents cannot
    // already have completed. Keep only these two current-frame public handles.
    ClothObservationTickCompletion = ClothTickFunction.GetCompletionHandle();
    ClothObservationEndCompletion = ClothEndTickFunction.GetCompletionHandle();
    if (!ClothObservationTickCompletion.IsValid() || !ClothObservationEndCompletion.IsValid()
        || ClothObservationTickCompletion->IsComplete() || ClothObservationEndCompletion->IsComplete())
    { ResetClothNativeObservation(); return; }
    ClothObservationArmedFrame = GFrameCounter;
    ClothObservationArmedRegistration = ClothObservationRegistration;
    ClothObservationAsset = GetSkeletalMeshAsset(); ClothObservationLeader = LeaderPoseComponent;
    ClothObservationLOD = GetPredictedLODLevel();
    for (const FClothingSimulationInstance& NativeSimulation : GetClothingSimulationInstances())
        ClothObservationInteractors.Add(NativeSimulation.GetClothingSimulationInteractor());
}

void UProject_JModularMeshComponent::ObserveClothNativeFrameCompletion()
{
    check(IsInGameThread());
    const UWorld* ClothWorld = GetWorld();
    if (!IsClothExecutionObservationEnabled() || !ClothWorld || ClothWorld->TickGroup != TG_NewlySpawned
        || ClothObservationArmedFrame != GFrameCounter || !IsClothNativeObservationCurrent())
    { ResetClothNativeObservation(); return; }
    if (ClothObservationQualifiedFrame == GFrameCounter) return;
    const bool bQualified = ClothObservationTickCompletion.IsValid() && ClothObservationEndCompletion.IsValid()
        && ClothObservationTickCompletion->IsComplete() && ClothObservationEndCompletion->IsComplete();
    ClothObservationTickCompletion.SafeRelease(); ClothObservationEndCompletion.SafeRelease();
    if (bQualified) ClothObservationQualifiedFrame = GFrameCounter;
    else ResetClothNativeObservation();
}

bool UProject_JModularMeshComponent::HasQualifiedClothNativeFrame(uint64 InFrame) const
{
    check(IsInGameThread());
    return IsClothExecutionObservationEnabled() && InFrame != MAX_uint64 && ClothObservationQualifiedFrame == InFrame
        && IsClothNativeObservationCurrent();
}

UProject_JModularMeshComponent::UProject_JModularMeshComponent()
{
	// Default optimization: don't tick animations if not rendered, relying on the main mesh's significance or leader pose.
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bEnableUpdateRateOptimizations = true;
}

void UProject_JModularMeshComponent::ReleaseClothExecutionLease()
{
	if (auto* OwnerActor = GetOwner())
		if (auto* Coordinator = OwnerActor->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>())
			Coordinator->ReleaseClothExecution(this);
}

void UProject_JModularMeshComponent::SetClothExecutionBudgetAllowed(bool bAllowed)
{
	ReleaseClothExecutionLease();
	bAllowClothExecutionBudget = bAllowed;
}

void UProject_JModularMeshComponent::SetComponentTickEnabled(bool bEnabled)
{
	if (!bEnabled) ReleaseClothExecutionLease();
	Super::SetComponentTickEnabled(bEnabled);
}

void UProject_JModularMeshComponent::OnPreUnregister()
{
	ReleaseClothExecutionLease();
	Super::OnPreUnregister();
}

void UProject_JModularMeshComponent::OnDestroyPhysicsState()
{
	// Before Super destroys simulator identity; no tick, recreation or wait here.
	ReleaseClothExecutionLease();
	Super::OnDestroyPhysicsState();
}

void UProject_JModularMeshComponent::AttachAndSetLeader(USkeletalMeshComponent* MainMesh)
{
	if (!MainMesh || MainMesh == this) return;
	ReleaseClothExecutionLease();
	if (!AttachToComponent(MainMesh, FAttachmentTransformRules::KeepRelativeTransform)) return;
	bFollowAttachParentPose = true;
	RefreshAttachParentPose();
}

bool UProject_JModularMeshComponent::CanCopyBasePose(const USkeletalMesh* Source,const USkeletalMesh* Target)
{
 if(!Source || !Target || !Source->GetSkeleton() || !Target->GetSkeleton())return false;
 const auto* A=Source->GetSkeleton();const auto* B=Target->GetSkeleton();
 const auto Has=[](const USkeleton* From,const USkeleton* To)
 {return From->GetCompatibleSkeletons().ContainsByPredicate([To](const TSoftObjectPtr<USkeleton>& Entry){return Entry.Get()==To;});};
 if(A!=B && !Has(A,B) && !Has(B,A))return false;
 const auto& S=Source->GetRefSkeleton();const auto& T=Target->GetRefSkeleton();
 if(S.GetNum()==0 || T.GetNum()==0 || S.GetBoneName(0)!=T.GetBoneName(0))return false;
 int32 Common=0;
 for(int32 Index=1;Index<T.GetNum();++Index)
 {
  const int32 Other=S.FindBoneIndex(T.GetBoneName(Index));if(Other==INDEX_NONE)continue;
  const int32 Parent=T.GetParentIndex(Index),OtherParent=S.GetParentIndex(Other);
  if(Parent==INDEX_NONE || OtherParent==INDEX_NONE || T.GetBoneName(Parent)!=S.GetBoneName(OtherParent))return false;
  ++Common;
 }
 return Common>0;
}

bool UProject_JModularMeshComponent::AttachAndCopyPose(USkeletalMeshComponent* MainMesh)
{
 if(!MainMesh || MainMesh==this || !CanCopyBasePose(MainMesh->GetSkeletalMeshAsset(),GetSkeletalMeshAsset()))return false;
 ReleaseClothExecutionLease();
 ResetClothNativeObservation();
 const bool PreviousAuto = bFollowAttachParentPose;
 bFollowAttachParentPose = false;
 if(!AttachToComponent(MainMesh,FAttachmentTransformRules::KeepRelativeTransform))
 {bFollowAttachParentPose=PreviousAuto;return false;}
 ClearPosePrerequisite();
 SetLeaderPoseComponent(nullptr);
 AddTickPrerequisiteComponent(MainMesh);PosePrerequisite=MainMesh;
 SetAnimInstanceClass(UProject_JCopyPoseAnimInstance::StaticClass());
 SetComponentTickEnabled(true);
 return true;
}

void UProject_JModularMeshComponent::ClearPosePrerequisite()
{
 if(auto* Source=PosePrerequisite.Get())RemoveTickPrerequisiteComponent(Source);
 PosePrerequisite.Reset();
}
void UProject_JModularMeshComponent::OnUnregister()
{
 ReleaseClothExecutionLease();
 ResetClothNativeObservation();
 if (++ClothObservationRegistration == 0) ++ClothObservationRegistration;
 ClearPosePrerequisite();
 // SetLeaderPoseComponent recreates physics while registered. The engine has
 // already destroyed physics before OnUnregister, so clear the leader only
 // after Super marks us unregistered and releases the clothing resources.
 Super::OnUnregister();
	if (bFollowAttachParentPose) SetLeaderPoseComponent(nullptr);
}
void UProject_JModularMeshComponent::OnRegister()
{
 ResetClothNativeObservation();
 if (++ClothObservationRegistration == 0) ++ClothObservationRegistration;
 // Super still creates cloth when rendering is impossible. Keep this runtime
 // visual-part creation policy before Super; preserve cook/preview/authored RHI values.
 const UWorld* RuntimeWorld = GetWorld();
 const bool bHeadlessGameWorld = RuntimeWorld &&
  (RuntimeWorld->WorldType == EWorldType::Game || RuntimeWorld->WorldType == EWorldType::PIE) &&
  !IsRunningCommandlet() && !FApp::CanEverRender();
 if (IsNetMode(NM_DedicatedServer) || bHeadlessGameWorld) SetAllowClothActors(false);
 Super::OnRegister();
	if (bFollowAttachParentPose)
	{
		RefreshAttachParentPose();
		return;
	}
 if(GetAnimClass()==UProject_JCopyPoseAnimInstance::StaticClass())
  if(auto* Source=Cast<USkeletalMeshComponent>(GetAttachParent()))
   if(CanCopyBasePose(Source->GetSkeletalMeshAsset(),GetSkeletalMeshAsset()))
   {AddTickPrerequisiteComponent(Source);PosePrerequisite=Source;SetComponentTickEnabled(true);}
}

void UProject_JModularMeshComponent::OnAttachmentChanged()
{
	ReleaseClothExecutionLease();
	ResetClothNativeObservation();
	Super::OnAttachmentChanged();
	if (bFollowAttachParentPose && IsRegistered()) RefreshAttachParentPose();
}

void UProject_JModularMeshComponent::RefreshAttachParentPose()
{
	ReleaseClothExecutionLease();
	ResetClothNativeObservation();
	// SCS establishes the attach parent before registration. Reattachment and
	// re-registration must replace the old source without a Tick or owner-name lookup.
	ClearPosePrerequisite();
	if (auto* Source = Cast<USkeletalMeshComponent>(GetAttachParent()))
	{
		if (GetAnimClass()) SetAnimInstanceClass(nullptr);
		// Rebinding an unchanged source must not recreate cloth or reset its history.
		SetLeaderPoseComponent(Source, LeaderPoseComponent.Get() != Source || bFollowerShouldTickPose, false);
	}
	else
	{
		SetLeaderPoseComponent(nullptr);
	}
	RefreshSharedPoseTick();
}

void UProject_JModularMeshComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (bFollowAttachParentPose) RefreshSharedPoseTick();
}

void UProject_JModularMeshComponent::RefreshSharedPoseTick()
{
	// Bone sharing does not replace Chaos Cloth's vertex simulation. Cloth needs
	// the component tick, but bFollowerShouldTickPose=false prevents another AnimGraph.
	// Respect the same creation admission as OnRegister, even before streamed
	// simulation resources exist. Metadata alone must not tick headless parts.
	const bool bNeedsCloth = LeaderPoseComponent.IsValid() && GetAllowClothActors() && !IsNetMode(NM_DedicatedServer) &&
		Project_J::Animation::UsesIndependentClothSimulation(*this);
	SetComponentTickEnabled(bNeedsCloth);
}

void UProject_JModularMeshComponent::SetSkeletalMesh(USkeletalMesh* NewMesh, bool bReinitPose)
{
	ReleaseClothExecutionLease();
	ResetClothNativeObservation();
	Super::SetSkeletalMesh(NewMesh, bReinitPose);
	if (bFollowAttachParentPose && IsRegistered()) RefreshAttachParentPose();
}
