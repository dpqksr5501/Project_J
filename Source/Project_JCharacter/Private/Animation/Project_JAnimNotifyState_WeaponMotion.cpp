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
		if (Presentation->BeginIndependentMotion(MotionKeys, PrimaryGripIKAlpha, SecondaryGripIKAlpha, TotalDuration, EntryBlendSeconds, ExitBlendSeconds))
		{
			Presentation->SetIndependentMotionPosition(0.0f);
			RuntimeStates.Add(MeshComp, FRuntimeState{});
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
			if (RuntimeStates.Contains(MeshComp))
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
	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			Presentation->EndIndependentMotion();
		}
	}
	RuntimeStates.Remove(MeshComp);
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
