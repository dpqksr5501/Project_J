#include "Animation/Project_JAnimNotify_SheatheWeapon.h"

#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

void UProject_JAnimNotify_SheatheWeapon::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (ACharacter* Character = MeshComp ? Cast<ACharacter>(MeshComp->GetOwner()) : nullptr)
	{
		if (UProject_JWeaponPresentationComponent* WeaponPresentation = Character->FindComponentByClass<UProject_JWeaponPresentationComponent>())
		{
			WeaponPresentation->AttachWeaponToSheathedSocket();
		}
	}
}
