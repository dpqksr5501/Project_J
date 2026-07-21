#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Project_JAnimNotify_SheatheWeapon.generated.h"

/** Moves the currently visible weapon from its drawn socket to its back socket. */
UCLASS(meta = (DisplayName = "Sheathe Weapon"))
class PROJECT_JCHARACTER_API UProject_JAnimNotify_SheatheWeapon : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
