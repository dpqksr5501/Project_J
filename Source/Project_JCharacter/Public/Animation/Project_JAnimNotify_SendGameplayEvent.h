#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimNotify_SendGameplayEvent.generated.h"

/**
 * Custom AnimNotify to bridge Motion Matching and Gameplay Ability System (GAS).
 * Triggers a GameplayEvent with a specific tag on the owning actor's AbilitySystemComponent.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JAnimNotify_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS")
	FGameplayTag EventTag;

	// Optional payload value to pass into the Gameplay Event
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS")
	float EventMagnitude = 1.0f;
};
