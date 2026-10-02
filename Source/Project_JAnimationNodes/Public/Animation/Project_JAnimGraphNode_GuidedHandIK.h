#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/Project_JAnimNode_GuidedHandIK.h"
#include "Project_JAnimGraphNode_GuidedHandIK.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UProject_JAnimGraphNode_GuidedHandIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FProject_JAnimNode_GuidedHandIK Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
