#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Audio/Project_JFoleyTypes.h"
#include "Project_JAnimNotify_FoleyEvent.generated.h"

/** Stateless timing bridge. Does not send GAS events or network RPCs. */
UCLASS(meta = (DisplayName = "Project J Foley Event"))
class PROJECT_JCHARACTER_API UProject_JAnimNotify_FoleyEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	UProject_JAnimNotify_FoleyEvent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	FProject_JFoleyEvent FoleyEvent;
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
