#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UProject_JAnimNotifyState_WeaponMotion::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			for (auto It = RuntimeStates.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
			if (auto* MeshStates = RuntimeStates.Find(MeshComp))
			{
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
	if (auto* MeshStates = RuntimeStates.Find(MeshComp))
	{
		FRuntimeState State;
		if (MeshStates->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), State))
		{
			if (auto* Presentation = State.Presentation.Get()) { Presentation->EndNotifyIndependentMotion(State.Token); }
		}
		if (MeshStates->IsEmpty()) { RuntimeStates.Remove(MeshComp); }
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
