#include "Animation/Project_JAnimNotifyState_ComboWindow.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JGameplayTags.h"

UProject_JAnimNotifyState_ComboWindow::UProject_JAnimNotifyState_ComboWindow()
{
	ComboWindowTag = FProject_JGameplayTags::Get().Event_Combat_ComboWindow;
}

void UProject_JAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AActor* Owner = MeshComp->GetOwner())
	{
		FGameplayEventData Payload;
		Payload.EventTag = ComboWindowTag;
		Payload.Instigator = Owner;
		Payload.Target = Owner;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, ComboWindowTag, Payload);
	}
}

void UProject_JAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
