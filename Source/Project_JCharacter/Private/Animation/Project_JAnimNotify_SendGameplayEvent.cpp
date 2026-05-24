#include "Animation/Project_JAnimNotify_SendGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/SkeletalMeshComponent.h"

void UProject_JAnimNotify_SendGameplayEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor) return;

	// In an MMORPG, avoid server overhead: predict visually on client, validate logically on server.
	// Only send event if we have a valid Ability System Component.
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor);
	if (ASC)
	{
		FGameplayEventData Payload;
		Payload.EventTag = EventTag;
		Payload.EventMagnitude = EventMagnitude;
		Payload.Instigator = OwnerActor;

		ASC->HandleGameplayEvent(EventTag, &Payload);
	}
}
