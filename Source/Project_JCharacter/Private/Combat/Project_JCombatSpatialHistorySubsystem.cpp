#include "Combat/Project_JCombatSpatialHistorySubsystem.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr double CadenceEpsilon = 1e-4;

double NextCaptureTime(double Previous, double Now, double Interval)
{
	const double Next = Previous + Interval;
	return Next > Now + CadenceEpsilon ? Next : Now + Interval;
}
}

void FProjectJCombatSpatialHistoryTickFunction::ExecuteTick(
	float DeltaTime,
	ELevelTick TickType,
	ENamedThreads::Type CurrentThread,
	const FGraphEventRef& MyCompletionGraphEvent)
{
	if (UProject_JCombatSpatialHistorySubsystem* Subsystem = Target.Get())
	{
		Subsystem->TickBatch(DeltaTime);
	}
}

bool UProject_JCombatSpatialHistorySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UProject_JCombatSpatialHistorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SSRSlots.Reserve(256);
	MultiPartSlots.Reserve(32);

	PostPhysicsTick.Target = this;
	PostPhysicsTick.TickGroup = TG_PostPhysics;
	PostPhysicsTick.EndTickGroup = TG_PostPhysics;
	PostPhysicsTick.bCanEverTick = true;
	PostPhysicsTick.bStartWithTickEnabled = false;
	PostPhysicsTick.bRunOnAnyThread = false;
	PostPhysicsTick.bAllowTickOnDedicatedServer = true;
	EnsureTickRegistered();
}

void UProject_JCombatSpatialHistorySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureTickRegistered();
	RefreshTickEnabled();
}

void UProject_JCombatSpatialHistorySubsystem::Deinitialize()
{
	if (PostPhysicsTick.IsTickFunctionRegistered())
	{
		PostPhysicsTick.UnRegisterTickFunction();
	}
	PostPhysicsTick.Target.Reset();
	SSRSlots.Reset();
	MultiPartSlots.Reset();
	Super::Deinitialize();
}

void UProject_JCombatSpatialHistorySubsystem::EnsureTickRegistered()
{
	if (PostPhysicsTick.IsTickFunctionRegistered())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World && !World->bIsTearingDown && World->PersistentLevel)
	{
		PostPhysicsTick.RegisterTickFunction(World->PersistentLevel);
	}
}

void UProject_JCombatSpatialHistorySubsystem::RefreshTickEnabled()
{
	EnsureTickRegistered();
	if (PostPhysicsTick.IsTickFunctionRegistered())
	{
		const bool bHasWork = !SSRSlots.IsEmpty() || !MultiPartSlots.IsEmpty();
		PostPhysicsTick.SetTickFunctionEnable(bHasWork);
	}
}

void UProject_JCombatSpatialHistorySubsystem::RegisterSSR(
	UProject_JServerSideRewindComponent* Component,
	AActor* Owner,
	UCapsuleComponent* Capsule,
	float IntervalSeconds)
{
	check(IsInGameThread());
	if (!IsValid(Component) || !IsValid(Owner))
	{
		return;
	}
	const double SafeInterval = FMath::IsFinite(IntervalSeconds) ? FMath::Clamp(static_cast<double>(IntervalSeconds), 1.0 / 120.0, 1.0) : (1.0 / 30.0);
	for (FSSRSlot& Existing : SSRSlots)
	{
		if (Existing.Component.Get() == Component)
		{
			Existing.Owner = Owner;
			Existing.Capsule = Capsule;
			Existing.Interval = SafeInterval;
			RefreshTickEnabled();
			return;
		}
	}

	FSSRSlot Slot;
	Slot.Component = Component;
	Slot.Owner = Owner;
	Slot.Capsule = Capsule;
	Slot.Interval = SafeInterval;
	Slot.NextSampleTime = -1.0;
	SSRSlots.Add(MoveTemp(Slot));
	RefreshTickEnabled();
}

void UProject_JCombatSpatialHistorySubsystem::UnregisterSSR(const UProject_JServerSideRewindComponent* Component)
{
	check(IsInGameThread());
	if (!Component)
	{
		return;
	}
	for (int32 i = SSRSlots.Num() - 1; i >= 0; --i)
	{
		if (!SSRSlots[i].Component.IsValid() || SSRSlots[i].Component.Get() == Component)
		{
			SSRSlots.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}
	RefreshTickEnabled();
}

void UProject_JCombatSpatialHistorySubsystem::RegisterMultiPart(
	UProject_JMultiPartHitComponent* Component,
	AActor* Owner,
	float IntervalSeconds)
{
	check(IsInGameThread());
	if (!IsValid(Component) || !IsValid(Owner))
	{
		return;
	}
	const double SafeInterval = FMath::IsFinite(IntervalSeconds) ? FMath::Clamp(static_cast<double>(IntervalSeconds), 1.0 / 120.0, 1.0) : (1.0 / 30.0);
	for (FMultiPartSlot& Existing : MultiPartSlots)
	{
		if (Existing.Component.Get() == Component)
		{
			Existing.Owner = Owner;
			Existing.Interval = SafeInterval;
			RefreshTickEnabled();
			return;
		}
	}

	FMultiPartSlot Slot;
	Slot.Component = Component;
	Slot.Owner = Owner;
	Slot.Interval = SafeInterval;
	Slot.NextSampleTime = -1.0;
	if (Component->HitParts.ContainsByPredicate([](const FProject_JBossHitPartSpec& Part) { return !Part.SocketOrBoneName.IsNone(); }))
	{
		if (USkeletalMeshComponent* Mesh = Component->ResolveOwnerMesh())
		{
			Slot.PosePrerequisite = Mesh;
			PostPhysicsTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
		}
	}
	MultiPartSlots.Add(MoveTemp(Slot));
	RefreshTickEnabled();
}

void UProject_JCombatSpatialHistorySubsystem::UnregisterMultiPart(const UProject_JMultiPartHitComponent* Component)
{
	check(IsInGameThread());
	if (!Component)
	{
		return;
	}
	for (int32 i = MultiPartSlots.Num() - 1; i >= 0; --i)
	{
		if (!MultiPartSlots[i].Component.IsValid() || MultiPartSlots[i].Component.Get() == Component)
		{
			RemoveMultiPartSlot(i);
		}
	}
	RefreshTickEnabled();
}

void UProject_JCombatSpatialHistorySubsystem::RemoveMultiPartSlot(int32 Index)
{
	USkeletalMeshComponent* Mesh = MultiPartSlots[Index].PosePrerequisite.Get();
	MultiPartSlots.RemoveAtSwap(Index, EAllowShrinking::No);
	if (Mesh && !MultiPartSlots.ContainsByPredicate([Mesh](const FMultiPartSlot& Slot) { return Slot.PosePrerequisite.Get() == Mesh; }))
	{
		PostPhysicsTick.RemovePrerequisite(Mesh, Mesh->PrimaryComponentTick);
	}
}

void UProject_JCombatSpatialHistorySubsystem::TickBatch(float DeltaTime)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_CombatSpatialHistory_BatchTick);
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();

	for (int32 i = SSRSlots.Num() - 1; i >= 0; --i)
	{
		FSSRSlot& Slot = SSRSlots[i];
		UProject_JServerSideRewindComponent* Comp = Slot.Component.Get();
		AActor* Owner = Slot.Owner.Get();
		if (!IsValid(Comp) || !Comp->IsRegistered() || Comp->IsBeingDestroyed() || !IsValid(Owner) || Owner->IsActorBeingDestroyed())
		{
			SSRSlots.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		if (!Comp->bCaptureRequested)
		{
			continue;
		}
		if (!Owner->HasAuthority())
		{
			Comp->ResetHistory();
			Slot.NextSampleTime = -1.0;
			continue;
		}
		if (Comp->PoseHistoryCount == 0 || Now + CadenceEpsilon >= Slot.NextSampleTime)
		{
			// Single actual sample per cadence/hitch; never fabricate catch-up poses.
			Slot.NextSampleTime = Comp->PoseHistoryCount == 0 ? Now + Slot.Interval : NextCaptureTime(Slot.NextSampleTime, Now, Slot.Interval);
			Comp->RecordBatchedSample(Now, Owner, Slot.Capsule.Get());
		}
	}

	for (int32 i = MultiPartSlots.Num() - 1; i >= 0; --i)
	{
		FMultiPartSlot& Slot = MultiPartSlots[i];
		UProject_JMultiPartHitComponent* Comp = Slot.Component.Get();
		AActor* Owner = Slot.Owner.Get();
		if (!IsValid(Comp) || !Comp->IsRegistered() || Comp->IsBeingDestroyed() || !IsValid(Owner) || Owner->IsActorBeingDestroyed())
		{
			RemoveMultiPartSlot(i);
			continue;
		}
		if (!Comp->bCaptureRequested)
		{
			continue;
		}
		if (!Owner->HasAuthority())
		{
			Comp->ResetHistory();
			Slot.NextSampleTime = -1.0;
			continue;
		}
		if (Comp->HistoryCount == 0 || Now + CadenceEpsilon >= Slot.NextSampleTime)
		{
			Slot.NextSampleTime = Comp->HistoryCount == 0 ? Now + Slot.Interval : NextCaptureTime(Slot.NextSampleTime, Now, Slot.Interval);
			Comp->RecordSample(Now);
		}
	}

	if (SSRSlots.IsEmpty() && MultiPartSlots.IsEmpty())
	{
		RefreshTickEnabled();
	}
}
