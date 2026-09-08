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
			Presentation->PlayCue(CueTag);
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
	if (AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr)
	{
		if (UProject_JCombatPresentationComponent* Presentation = OwnerActor->FindComponentByClass<UProject_JCombatPresentationComponent>())
		{
			UE_LOG(LogProjectJCombatPresentationNotify, Verbose, TEXT("[CombatVFX] Notify end. Owner=%s Animation=%s Cue=%s"),
				*GetNameSafe(OwnerActor), *GetNameSafe(Animation), *CueTag.ToString());
			Presentation->StopCue(CueTag);
			return;
		}
		UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify end ignored: presentation component missing. Owner=%s Animation=%s Cue=%s"),
			*GetNameSafe(OwnerActor), *GetNameSafe(Animation), *CueTag.ToString());
		return;
	}
	UE_LOG(LogProjectJCombatPresentationNotify, Warning, TEXT("[CombatVFX] Notify end ignored: mesh or owner missing. Animation=%s Cue=%s"),
		*GetNameSafe(Animation), *CueTag.ToString());
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
