#include "Animation/Project_JAnimNotify_SheatheWeapon.h"

#include "Components/SkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"

void UProject_JAnimNotify_SheatheWeapon::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (MeshComp)
	{
		if (AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(MeshComp->GetOwner()))
		{
			PlayerCharacter->MoveWeaponToSheathedSocket();
		}
	}
}
