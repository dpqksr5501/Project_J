#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "IAnimationBudgetAllocator.h"
#include "UObject/UObjectIterator.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCharacterABA, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarCharacterABA(TEXT("ProjectJ.AnimationBudget.Enabled"), 1,
	TEXT("Use ABA for eligible non-local, non-combat Character meshes. Requires a.Budget.Enabled=1. 0 restores normal mesh ticking."));
TAutoConsoleVariable<int32> CVarFullRemoteCloths(TEXT("ProjectJ.ClothBudget.MaxFullRemoteParts"),4,
	TEXT("Maximum nearby remote cloth components granted authored simulation quality. Local players do not consume this cosmetic quota."));
FAutoConsoleCommandWithWorld StatusCommand(TEXT("ProjectJ.AnimationBudget.Status"), TEXT("Report current world's Character ABA ownership."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const auto* Service = World ? World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>() : nullptr;
		if (Service)
		{
			UE_LOG(LogProjectJCharacterABA, Display, TEXT("World=%s NetMode=%d Enabled=%d Tracked=%d Managed=%d"),
				*World->GetName(), int32(World->GetNetMode()), Service->IsEnabledForWorld(), Service->GetTrackedCount(), Service->GetManagedCount());
		}
	}));
}
TRACE_DECLARE_INT_COUNTER(CharacterABAManaged, TEXT("ProjectJ/CharacterABA/Managed"));
TRACE_DECLARE_INT_COUNTER(CharacterABAProtected, TEXT("ProjectJ/CharacterABA/CombatProtected"));
TRACE_DECLARE_INT_COUNTER(AnimationObserverViews, TEXT("ProjectJ/Animation/LocalObserverViews"));
TRACE_DECLARE_INT_COUNTER(ClothExecutionReserved, TEXT("ProjectJ/ClothExecution/ReservedVertices"));
TRACE_DECLARE_INT_COUNTER(ClothExecutionProtected, TEXT("ProjectJ/ClothExecution/ProtectedVertices"));
TRACE_DECLARE_INT_COUNTER(ClothExecutionDebt, TEXT("ProjectJ/ClothExecution/DebtVertices"));
TRACE_DECLARE_INT_COUNTER(ClothExecutionUnknown, TEXT("ProjectJ/ClothExecution/UnknownParts"));

bool UProject_JCharacterAnimationBudgetSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UProject_JCharacterAnimationBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::OnPostActorTick);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}
bool UProject_JCharacterAnimationBudgetSubsystem::IsEnabledForWorld() const
{
	// Engine/plugin CVars outlive this world service; avoid a registry lookup per
	// policy pass (and per PIE world) in large character workloads.
	static const auto* EngineEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("a.Budget.Enabled"));
	return !bEnding && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_DedicatedServer
		&& EnabledOverride.Get(CVarCharacterABA.GetValueOnGameThread() != 0) && EngineEnabled && EngineEnabled->GetInt() == 1;
}
void UProject_JCharacterAnimationBudgetSubsystem::SetEnabledOverride(TOptional<bool> Enabled)
{ EnabledOverride = Enabled; Refresh(); }
void UProject_JCharacterAnimationBudgetSubsystem::RegisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if (bEnding || !IsValid(Mesh) || Mesh->GetWorld() != GetWorld() || Meshes.Contains(Mesh)) { return; }
	Meshes.RemoveAllSwap([](const auto& Entry) { return !Entry.IsValid(); });
	if (Meshes.Num() < MaxMeshes) { Meshes.Add(Mesh); }
}
void UProject_JCharacterAnimationBudgetSubsystem::UnregisterMesh(UProject_JBudgetedSkeletalMeshComponent* Mesh)
{
	if (Mesh && IsValid(Mesh->GetOwner()))
		if (auto* Coordinator=Mesh->GetOwner()->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>())
			Coordinator->ReleaseClothExecution();
	if (Mesh) { Mesh->ObservedDistanceTier.Reset(); Mesh->ObservedFullClothQuality.Reset(); Mesh->LeaveBudget(); }
	Meshes.RemoveAllSwap([Mesh](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Mesh; });
}
int32 UProject_JCharacterAnimationBudgetSubsystem::GetManagedCount() const
{
	int32 Count = 0;
	for (const auto& Entry : Meshes) { Count += Entry.IsValid() && Entry->IsManagedByBudget(); }
	return Count;
}
void UProject_JCharacterAnimationBudgetSubsystem::OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if(World!=GetWorld()) return;
	const TGuardValue<bool> ObserveGuard(bObserveNativeClothFrame,TickType==LEVELTICK_All &&
		FMath::IsFinite(DeltaSeconds) && DeltaSeconds>0 && UProject_JModularMeshComponent::IsClothExecutionObservationEnabled());
	Refresh();
}
void UProject_JCharacterAnimationBudgetSubsystem::Refresh()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CharacterABA_Policy);
	const bool bEnabled = IsEnabledForWorld();
	auto* Allocator = bEnabled ? IAnimationBudgetAllocator::Get(GetWorld()) : nullptr;
	const bool bTickWhenNotRendered = Project_J::Anim::GetBudgetTickWhenNotRendered();
	// Reuse this world's existing GT pass even when ABA is disabled: MM and
	// visual cadence also consume observation. No Actor reads run on workers.
	TArray<FVector, TInlineAllocator<2>> Views;
	if (!Meshes.IsEmpty() && !bEnding && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const auto* Controller = It->Get();
			if (!IsValid(Controller) || !Controller->IsLocalPlayerController()) continue;
			FVector Location; FRotator Rotation;
			Controller->GetPlayerViewPoint(Location, Rotation);
			if (!Location.ContainsNaN()) Views.Add(Location);
		}
	}
	TRACE_COUNTER_SET(AnimationObserverViews, Views.Num());
	int32 Managed = 0, Protected = 0;
	struct FClothCandidate { UProject_JBudgetedSkeletalMeshComponent* Mesh; double Score; int32 Parts; uint32 Id; };
	TArray<FClothCandidate,TInlineAllocator<16>> ClothCandidates;
	struct FExecutionCandidate
	{
		TWeakObjectPtr<UProject_JAnimationUpdateCoordinatorComponent> Coordinator;
		TWeakObjectPtr<UProject_JBudgetedSkeletalMeshComponent> Source;
		FProjectJClothExecutionDemand Demand;
		double Score=0;
		uint32 Id=0;
		bool bProtected=true, bDesired=false;
	};
	TArray<FExecutionCandidate,TInlineAllocator<16>> ExecutionCandidates;
	const int64 ExecutionCap=UProject_JAnimationUpdateCoordinatorComponent::GetClothExecutionVertexCap();
	const bool bExecutionEnabled=ExecutionCap>0 && GetWorld() && !bEnding &&
		!GetWorld()->bIsTearingDown && GetWorld()->GetNetMode()!=NM_DedicatedServer;
	for (int32 Index = Meshes.Num() - 1; Index >= 0; --Index)
	{
		auto* Mesh = Meshes[Index].Get();
		if (!Mesh) { Meshes.RemoveAtSwap(Index); continue; }
		auto* Coordinator=(bObserveNativeClothFrame || !Views.IsEmpty()) && IsValid(Mesh->GetOwner())
			? Mesh->GetOwner()->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>() : nullptr;
		if(bObserveNativeClothFrame && Coordinator) Coordinator->ObserveClothNativeFrameCompletions();
		const bool bPreviouslyFull=Mesh->ObservedFullClothQuality.Get(false);
		Mesh->ObservedFullClothQuality.Reset();
		Mesh->ObservedDistanceTier.Reset();
		double NearestSquared = TNumericLimits<double>::Max();
		if (!Views.IsEmpty() && IsValid(Mesh->GetOwner()))
		{
			const FVector Position = Mesh->GetOwner()->GetActorLocation();
			bool bHasFiniteDistance = false;
			for (const FVector& View : Views)
			{
				const double Squared = FVector::DistSquared(Position, View);
				if (FMath::IsFinite(Squared)) { NearestSquared = FMath::Min(NearestSquared, Squared); bHasFiniteDistance = true; }
			}
			const auto* Native = Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance());
			const auto Settings = Native ? Native->GetEffectiveAnimationBudgetSettings() : FProject_JAnimationBudgetSettings();
			Mesh->ObservedDistanceTier = !bHasFiniteDistance ? 0.f : ProjectJ::Animation::ResolveAnimationDistanceTier(NearestSquared, Settings);
			if(Coordinator && Coordinator->IsVisualBudgetEnabled() && Mesh->bAllowProjectBudget && Coordinator->GetManagedClothCount()>0)
			{
				const bool bValidSettings=FMath::IsFinite(Settings.NearDistance) && FMath::IsFinite(Settings.MidDistance) &&
					FMath::IsFinite(Settings.FarDistance) && Settings.NearDistance>0 && Settings.MidDistance>Settings.NearDistance && Settings.FarDistance>Settings.MidDistance;
				const bool bLocal=ProjectJ::Animation::IsLocalPlayerAvatar(Cast<APawn>(Mesh->GetOwner()));
				if(bLocal || !bHasFiniteDistance || !bValidSettings) Mesh->ObservedFullClothQuality=true;
				else
				{
					Mesh->ObservedFullClothQuality=false;
					if(Mesh->ObservedDistanceTier.GetValue()==0.f)
						ClothCandidates.Add({Mesh,NearestSquared*(bPreviouslyFull?.81:1.),Coordinator->GetManagedClothCount(),Mesh->GetUniqueID()});
				}
			}
		}
		if(Coordinator && bExecutionEnabled && Coordinator->IsClothExecutionSource(Mesh))
		{
			FExecutionCandidate Candidate;
			Candidate.Coordinator=Coordinator; Candidate.Source=Mesh; Candidate.Id=Mesh->GetUniqueID();
			// A missing/invalid observer observation preserves execution. Ordinary
			// SP/AI identity is decided separately, not inferred from an absent PC.
			Candidate.bProtected=!Mesh->ObservedDistanceTier.IsSet();
			Candidate.Score=NearestSquared;
			Candidate.bProtected|=!FMath::IsFinite(Candidate.Score);
			ExecutionCandidates.Add(Candidate);
		}
		Mesh->RefreshAnimationUpdateRequirements();
		Mesh->RefreshMontageUpdateRequirement();
		Protected += Mesh->IsCombatCritical();
		if (!bEnabled || !Mesh->CanUseBudget()) { Mesh->LeaveBudget(); continue; }
		if (!Allocator) { continue; }
		if (!Allocator->GetEnabled()) { Allocator->SetEnabled(true); bOwnAllocatorEnable = true; }
		Mesh->EnterBudget();
		if (Mesh->IsManagedByBudget())
		{
			const float Tier = Mesh->GetAnimationDistanceTier();
			Mesh->bBudgetTickWhenNotRendered = bTickWhenNotRendered;
			Allocator->SetComponentSignificance(Mesh, 1.0f / (1.0f + Tier), false, Mesh->bBudgetTickWhenNotRendered, false, false);
			++Managed;
		}
	}
	// Reuse the existing observer pass and source registry. A protected montage
	// still retains its gameplay pose; it does not reserve unlimited cosmetic cloth.
	ClothCandidates.Sort([](const FClothCandidate& A,const FClothCandidate& B)
	{ return A.Score==B.Score?A.Id<B.Id:A.Score<B.Score; });
	int32 Remaining=FMath::Clamp(CVarFullRemoteCloths.GetValueOnGameThread(),0,32);
	for(const auto& Candidate:ClothCandidates) if(Candidate.Parts<=Remaining)
	{ Candidate.Mesh->ObservedFullClothQuality=true;Remaining-=Candidate.Parts; }
	// Existing world registry/camera pass owns both desired work and actual
	// admission. Do not reuse a fading-out slot before its native drain closes.
	int64 ReservedCost=0, ProtectedCost=0; int32 UnknownParts=0;
	const auto SumCost=[](int64 A,int64 B) { return A>MAX_int64-B?MAX_int64:A+B; };
	for(auto& Candidate:ExecutionCandidates)
	{
		auto* Coordinator=Candidate.Coordinator.Get(); auto* Source=Candidate.Source.Get();
		if(!Coordinator || !Source) continue;
		Candidate.bProtected|=Source->HasFullClothQualityGrant() ||
			ProjectJ::Animation::IsClothExecutionProtected(Cast<APawn>(Source->GetOwner()));
		Candidate.Demand=Coordinator->PrepareClothExecutionDemand(bExecutionEnabled && Source->bAllowProjectBudget,Candidate.bProtected);
		ProtectedCost=SumCost(ProtectedCost,Candidate.Demand.Protected);
		ReservedCost=SumCost(ReservedCost,SumCost(Candidate.Demand.Reserved,Candidate.Demand.Protected));
		UnknownParts+=Candidate.Demand.Unknown;
		Candidate.Score*=Candidate.Demand.bPreviouslyDesired?.81:1.;
	}
	ExecutionCandidates.Sort([](const FExecutionCandidate& A,const FExecutionCandidate& B)
	{ return A.Score==B.Score?A.Id<B.Id:A.Score<B.Score; });
	int64 DesiredRemaining=FMath::Max(int64(0),ExecutionCap-FMath::Min(ExecutionCap,ProtectedCost));
	int64 ResumeRemaining=FMath::Max(int64(0),ExecutionCap-FMath::Min(ExecutionCap,ReservedCost));
	for(auto& Candidate:ExecutionCandidates)
	{
		auto* Coordinator=Candidate.Coordinator.Get();
		if(!Coordinator) continue;
		Candidate.bDesired=Candidate.bProtected || (Candidate.Demand.Eligible>0 && Candidate.Demand.Eligible<=DesiredRemaining);
		if(!Candidate.bProtected && Candidate.bDesired) DesiredRemaining-=Candidate.Demand.Eligible;
		const bool bMayResume=Candidate.bProtected || (Candidate.bDesired && UnknownParts==0 && Candidate.Demand.Resume<=ResumeRemaining);
		if(!Candidate.bProtected && bMayResume)
		{
			ResumeRemaining-=Candidate.Demand.Resume;
			ReservedCost=SumCost(ReservedCost,Candidate.Demand.Resume);
		}
		Coordinator->SetClothExecutionGrant(Candidate.bDesired,bMayResume,Candidate.bProtected);
	}
	TRACE_COUNTER_SET(ClothExecutionReserved,bExecutionEnabled?ReservedCost:0);
	TRACE_COUNTER_SET(ClothExecutionProtected,bExecutionEnabled?ProtectedCost:0);
	TRACE_COUNTER_SET(ClothExecutionDebt,bExecutionEnabled?FMath::Max(int64(0),ReservedCost-ExecutionCap):0);
	TRACE_COUNTER_SET(ClothExecutionUnknown,bExecutionEnabled?UnknownParts:0);
	// Counters describe the last processed world; PIE worlds are distinguished by CPU scope context.
	TRACE_COUNTER_SET(CharacterABAManaged, Managed);
	TRACE_COUNTER_SET(CharacterABAProtected, Protected);
	if (!bEnabled) { RestoreAllocatorIfUnused(); }
}
void UProject_JCharacterAnimationBudgetSubsystem::RestoreAllocatorIfUnused()
{
	if (!bOwnAllocatorEnable || GetManagedCount() != 0) { return; }
	// Conservative ownership check only on disable/teardown, never the per-frame path.
	// Unknown budgeted components may be owned by another feature; do not disable it.
	for (TObjectIterator<USkeletalMeshComponentBudgeted> It; It; ++It)
	{
		if (It->IsTemplate() || !It->IsRegistered() || It->GetWorld() != GetWorld()) { continue; }
		const auto* ProjectMesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(*It);
		if (!ProjectMesh || ProjectMesh->IsManagedByBudget()) { bOwnAllocatorEnable = false; return; }
	}
	if (auto* Allocator = IAnimationBudgetAllocator::Get(GetWorld())) { Allocator->SetEnabled(false); }
	bOwnAllocatorEnable = false;
}
void UProject_JCharacterAnimationBudgetSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (World != GetWorld() || bEnding) { return; }
	bEnding = true;
	for (const auto& Entry:Meshes)
		if (Entry.IsValid() && IsValid(Entry->GetOwner()))
			if (auto* Coordinator=Entry->GetOwner()->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>())
				Coordinator->ReleaseClothExecution();
	for (const auto& Entry : Meshes) { if (Entry.IsValid()) { Entry->ObservedDistanceTier.Reset(); Entry->ObservedFullClothQuality.Reset(); Entry->LeaveBudget(); } }
	RestoreAllocatorIfUnused();
	Meshes.Reset();
}
void UProject_JCharacterAnimationBudgetSubsystem::Deinitialize()
{
	OnWorldCleanup(GetWorld(), false, false);
	FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	Super::Deinitialize();
}
