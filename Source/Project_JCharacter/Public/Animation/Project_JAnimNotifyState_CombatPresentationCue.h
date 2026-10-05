#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimNotifyState_CombatPresentationCue.generated.h"

/** Animation timing only. The character presentation component owns VFX lifetime. */
UCLASS(meta = (DisplayName = "Project J Combat Presentation Cue"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_CombatPresentationCue : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FGameplayTag CueTag;
	struct FCueLease
	{
		TWeakObjectPtr<class UProject_JCombatPresentationComponent> Presentation;
		uint64 Token = 0;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TMap<int32, FCueLease>> Leases;
};
