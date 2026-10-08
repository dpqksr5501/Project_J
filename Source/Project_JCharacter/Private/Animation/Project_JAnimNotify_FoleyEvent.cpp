#include "Animation/Project_JAnimNotify_FoleyEvent.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Components/Project_JFoleyComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UProject_JAnimNotify_FoleyEvent::UProject_JAnimNotify_FoleyEvent()
{
	FoleyEvent.Event = Project_J::Foley::Walk;
}

void UProject_JAnimNotify_FoleyEvent::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp || UAnimNotifyLibrary::IsBlendingOut(EventReference)) { return; }
	if (AActor* Owner = MeshComp->GetOwner())
	{
		if (auto* Foley = Owner->FindComponentByClass<UProject_JFoleyComponent>())
		{
			Foley->HandleAnimNotify(MeshComp, FoleyEvent);
		}
	}
}

FString UProject_JAnimNotify_FoleyEvent::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Foley: %s (%s)"), *FoleyEvent.Event.ToString(),
		*UEnum::GetValueAsString(FoleyEvent.Side));
}
