#include "Animation/Project_JAnimNotifyState_WeaponGroundContact.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UProject_JAnimNotifyState_WeaponGroundContact::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* Presentation = Owner->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			for (auto It = RuntimeStates.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) It.RemoveCurrent(); }
			auto& MeshStates = RuntimeStates.FindOrAdd(MeshComp);
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
	if (auto* MeshStates = RuntimeStates.Find(MeshComp))
	{
		FRuntimeState State;
		const bool bRemoved = MeshStates->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), State);
		if (MeshStates->IsEmpty()) RuntimeStates.Remove(MeshComp);
		if (bRemoved) { if (auto* Presentation = State.Presentation.Get()) Presentation->EndGroundContactNotify(State.Token); }
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
