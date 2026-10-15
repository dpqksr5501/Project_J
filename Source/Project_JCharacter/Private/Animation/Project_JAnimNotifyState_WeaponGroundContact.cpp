#include "Animation/Project_JAnimNotifyState_WeaponGroundContact.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "UObject/UObjectGlobals.h"

TRACE_DECLARE_INT_COUNTER(GroundContactNotifyBegins, TEXT("ProjectJ/Notify/GroundContact/Begins"));
TRACE_DECLARE_INT_COUNTER(GroundContactNotifyEnds, TEXT("ProjectJ/Notify/GroundContact/Ends"));
TRACE_DECLARE_INT_COUNTER(GroundContactWeakMeshVisits, TEXT("ProjectJ/Notify/GroundContact/WeakMeshVisits"));
TRACE_DECLARE_INT_COUNTER(GroundContactInstanceVisits, TEXT("ProjectJ/Notify/GroundContact/InstanceVisits"));

void UProject_JAnimNotifyState_WeaponGroundContact::BeginDestroy()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
	Super::BeginDestroy();
}

void UProject_JAnimNotifyState_WeaponGroundContact::PruneCollectedMeshes()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_GroundContactNotify_WeakMeshPrune);
	TRACE_COUNTER_ADD(GroundContactWeakMeshVisits, RuntimeStates.Num());
	for (auto It = RuntimeStates.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) It.RemoveCurrent(); }
}

void UProject_JAnimNotifyState_WeaponGroundContact::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_GroundContactNotify_Begin);
	TRACE_COUNTER_INCREMENT(GroundContactNotifyBegins);
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			if (!PostGCHandle.IsValid()) PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ThisClass::PruneCollectedMeshes);
			auto& MeshStates = RuntimeStates.FindOrAdd(MeshComp);
			TRACE_COUNTER_ADD(GroundContactInstanceVisits, MeshStates.Num());
			for (auto It = MeshStates.CreateIterator(); It; ++It)
			{
				if (!It.Value().Presentation.IsValid() || !It.Value().Presentation->IsGroundContactNotifyCurrent(It.Value().Token))
					It.RemoveCurrent();
			}
			const int32 InstanceID = EventReference.GetNotifyInstanceID();
			if (!MeshStates.Contains(InstanceID))
			{
				if (const uint64 Token = Presentation->BeginGroundContactNotify()) MeshStates.Add(InstanceID, {Presentation, Token});
			}
		}
	}
}

void UProject_JAnimNotifyState_WeaponGroundContact::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_GroundContactNotify_End);
	TRACE_COUNTER_INCREMENT(GroundContactNotifyEnds);
	if (auto* MeshStates = RuntimeStates.Find(MeshComp))
	{
		FRuntimeState State;
		const bool bRemoved = MeshStates->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), State);
		if (MeshStates->IsEmpty()) RuntimeStates.Remove(MeshComp);
		if (bRemoved) { if (auto* Presentation = State.Presentation.Get()) Presentation->EndGroundContactNotify(State.Token); }
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
