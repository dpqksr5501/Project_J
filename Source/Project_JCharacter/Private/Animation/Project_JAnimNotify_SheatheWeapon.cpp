#include "Animation/Project_JAnimNotify_SheatheWeapon.h"
#include "GameFramework/Pawn.h"
#include "Movement/Project_JMovementQueries.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

void UProject_JAnimNotify_SheatheWeapon::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (APawn* Character = MeshComp ? Cast<APawn>(MeshComp->GetOwner()) : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* WeaponPresentation = Character->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			WeaponPresentation->AttachWeaponToSheathedSocket();
		}
	}
}
