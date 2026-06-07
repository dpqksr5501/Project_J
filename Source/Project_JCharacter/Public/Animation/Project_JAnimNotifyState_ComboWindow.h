#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimNotifyState_ComboWindow.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Combo Window"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UProject_JAnimNotifyState_ComboWindow();

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

protected:
	/** The gameplay event tag to send to the owner when the window opens */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	FGameplayTag ComboWindowTag;
};
