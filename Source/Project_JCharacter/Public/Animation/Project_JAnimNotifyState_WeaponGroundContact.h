#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Project_JAnimNotifyState_WeaponGroundContact.generated.h"

/**
 * Enables terrain correction only for the authored dragging part of a weapon
 * motion window. It may overlap Weapon Motion and contains no per-frame data.
 */
UCLASS(meta = (DisplayName = "Weapon Ground Contact"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_WeaponGroundContact : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
