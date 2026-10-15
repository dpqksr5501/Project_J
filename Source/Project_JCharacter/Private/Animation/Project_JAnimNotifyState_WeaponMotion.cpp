#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "UObject/UObjectGlobals.h"

TRACE_DECLARE_INT_COUNTER(WeaponMotionNotifyBegins, TEXT("ProjectJ/Notify/WeaponMotion/Begins"));
TRACE_DECLARE_INT_COUNTER(WeaponMotionNotifyTicks, TEXT("ProjectJ/Notify/WeaponMotion/Ticks"));
TRACE_DECLARE_INT_COUNTER(WeaponMotionNotifyEnds, TEXT("ProjectJ/Notify/WeaponMotion/Ends"));
TRACE_DECLARE_INT_COUNTER(WeaponMotionWeakMeshVisits, TEXT("ProjectJ/Notify/WeaponMotion/WeakMeshVisits"));
TRACE_DECLARE_INT_COUNTER(WeaponMotionInstanceVisits, TEXT("ProjectJ/Notify/WeaponMotion/InstanceVisits"));

void UProject_JAnimNotifyState_WeaponMotion::BeginDestroy()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
	Super::BeginDestroy();
}

void UProject_JAnimNotifyState_WeaponMotion::PruneCollectedMeshes()
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponMotionNotify_WeakMeshPrune);
	TRACE_COUNTER_ADD(WeaponMotionWeakMeshVisits, RuntimeStates.Num());
	for (auto It = RuntimeStates.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) It.RemoveCurrent(); }
}

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
EDataValidationResult UProject_JAnimNotifyState_WeaponMotion::IsDataValid(FDataValidationContext& Context) const
{
	const auto Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	if (const FName Failure = Project_J::WeaponMotion::GetValidationFailure(MotionKeys, PrimaryGripIKAlpha,
		SecondaryGripIKAlpha, 0.f, EntryBlendSeconds, ExitBlendSeconds); !Failure.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
			NSLOCTEXT("ProjectJWeaponMotion", "InvalidMotion", "Weapon Motion {0} must be finite and in its authored range, with normalized rotations and unique ascending key times."), FText::FromName(Failure)));
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif

void UProject_JAnimNotifyState_WeaponMotion::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponMotionNotify_Begin);
	TRACE_COUNTER_INCREMENT(WeaponMotionNotifyBegins);
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			// Weak keys retire at GC. Re-scanning every living mesh for each
			// new participant makes a shared asset's burst quadratic.
			if (!PostGCHandle.IsValid()) PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ThisClass::PruneCollectedMeshes);
			if (auto* MeshStates = RuntimeStates.Find(MeshComp))
			{
				TRACE_COUNTER_ADD(WeaponMotionInstanceVisits, MeshStates->Num());
				for (auto It = MeshStates->CreateIterator(); It; ++It)
				{
					if (!It.Value().Presentation.IsValid() || !It.Value().Presentation->IsNotifyIndependentMotionCurrent(It.Value().Token)) { It.RemoveCurrent(); }
				}
				if (MeshStates->Contains(EventReference.GetNotifyInstanceID())) { return; }
			}
			const uint64 Token = Presentation->BeginNotifyIndependentMotion(MotionKeys, PrimaryGripIKAlpha, SecondaryGripIKAlpha,
				TotalDuration, EntryBlendSeconds, ExitBlendSeconds);
			if (Token)
			{
				Presentation->SetIndependentMotionPosition(0.0f);
				RuntimeStates.FindOrAdd(MeshComp).Add(EventReference.GetNotifyInstanceID(), {Presentation, Token});
			}
		}
	}
}

void UProject_JAnimNotifyState_WeaponMotion::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponMotionNotify_Tick);
	TRACE_COUNTER_INCREMENT(WeaponMotionNotifyTicks);
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			const auto* MeshStates = RuntimeStates.Find(MeshComp);
			const auto* State = MeshStates ? MeshStates->Find(EventReference.GetNotifyInstanceID()) : nullptr;
			if (State && State->Presentation.Get() == Presentation && Presentation->IsNotifyIndependentMotionCurrent(State->Token))
			{
				// Do not integrate FrameDeltaTime here. Montage blending, time dilation and
				// animation update-rate policies can make a locally integrated clock differ
				// from the pose being rendered. Persona uses the notify event's true montage
				// position, so runtime must use that exact same source of time.
				const FAnimNotifyEvent* Event = EventReference.GetNotify();
				const float NormalizedTime = Event && Event->GetDuration() > UE_KINDA_SMALL_NUMBER
					? FMath::Clamp((EventReference.GetCurrentAnimationTime() - Event->GetTime()) / Event->GetDuration(), 0.0f, 1.0f)
					: 1.0f;
				Presentation->RefreshIndependentMotionKeys(MotionKeys, PrimaryGripIKAlpha, SecondaryGripIKAlpha, Event ? Event->GetDuration() : 0.0f, EntryBlendSeconds, ExitBlendSeconds);
				Presentation->SetIndependentMotionPosition(NormalizedTime);
			}
		}
	}
}

void UProject_JAnimNotifyState_WeaponMotion::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WeaponMotionNotify_End);
	TRACE_COUNTER_INCREMENT(WeaponMotionNotifyEnds);
	if (auto* MeshStates = RuntimeStates.Find(MeshComp))
	{
		FRuntimeState State;
		const bool bRemoved = MeshStates->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), State);
		if (MeshStates->IsEmpty()) { RuntimeStates.Remove(MeshComp); }
		// Presentation cleanup can re-enter or collect garbage. Return the
		// registry entry before calling it, retaining no pointer into the map.
		if (bRemoved) { if (auto* Presentation = State.Presentation.Get()) Presentation->EndNotifyIndependentMotion(State.Token); }
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
