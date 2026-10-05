#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"

#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatPresentationNotify, Log, All);

void UProject_JAnimNotifyState_CombatPresentationCue::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JCombatPresentationComponent* Presentation = OwnerActor->FindComponentByClass<UProject_JCombatPresentationComponent>())
		{
			UE_LOG(LogProjectJCombatPresentationNotify, Verbose, TEXT("[CombatVFX] Notify begin. Owner=%s Animation=%s Cue=%s Duration=%.3f"),
				*GetNameSafe(OwnerActor), *GetNameSafe(Animation), *CueTag.ToString(), TotalDuration);
			for (auto It = Leases.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
			auto& MeshLeases = Leases.FindOrAdd(MeshComp);
			for (auto It = MeshLeases.CreateIterator(); It; ++It)
			{
				if (!It.Value().Presentation.IsValid() || !It.Value().Presentation->IsCueLeaseCurrent(It.Value().Token)) { It.RemoveCurrent(); }
			}
			const int32 ID = EventReference.GetNotifyInstanceID();
			if (!MeshLeases.Contains(ID))
			{
				const uint64 Token = Presentation->BeginCueLease(CueTag);
				if (Token) { MeshLeases.Add(ID, {Presentation, Token}); }
			}
			return;
		}
		UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify begin ignored: presentation component missing. Owner=%s Animation=%s Cue=%s"),
			*GetNameSafe(OwnerActor), *GetNameSafe(Animation), *CueTag.ToString());
		return;
	}
	UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify begin ignored: mesh or owner missing. Animation=%s Cue=%s"),
		*GetNameSafe(Animation), *CueTag.ToString());
}

void UProject_JAnimNotifyState_CombatPresentationCue::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (auto* MeshLeases = Leases.Find(MeshComp))
	{
		FCueLease Lease;
		const bool bFound = MeshLeases->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), Lease);
		if (MeshLeases->IsEmpty()) { Leases.Remove(MeshComp); }
		if (bFound && Lease.Presentation.IsValid()) { Lease.Presentation->EndCueLease(Lease.Token); }
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
