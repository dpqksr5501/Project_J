#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
    // An asset or simulation replacement owns a new configuration. Never write
    // the previous garment's defaults into that new simulation.
    if(bReduced && IsCurrent()) Apply(false);
    if(bAddedCoordinatorPrerequisite && Coordinator && Mesh.IsValid()) Mesh->RemoveTickPrerequisiteComponent(Coordinator);
    Bindings.Reset(); Mesh.Reset(); Asset.Reset(); bReduced=bAppliedDiscrete=false;
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
void UProject_JAnimationUpdateCoordinatorComponent::RestoreClothBudget()
{
    for(auto& Entry:ClothMeshes) Entry.Restore(this);
    ClothMeshes.Reset(); ClothDowngradeSince=-1;
}
void UProject_JAnimationUpdateCoordinatorComponent::RefreshClothBudget(UProject_JBudgetedSkeletalMeshComponent* Source,bool bLocal)
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
}
