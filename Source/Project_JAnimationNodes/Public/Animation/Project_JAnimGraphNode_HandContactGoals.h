#pragma once
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/Project_JAnimNode_HandContactGoals.h"
#include "Project_JAnimGraphNode_HandContactGoals.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UProject_JAnimGraphNode_HandContactGoals : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,Category="Settings")
	FProject_JAnimNode_HandContactGoals Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override { return GetControllerDescription(); }
	virtual FText GetTooltipText() const override
	{ return NSLOCTEXT("ProjectJHandContactGoals","Tooltip","Publish wrist goals from the current input pose; contact alpha is applied once before body IK. Does not solve the body or retarget animation."); }
protected:
	virtual FText GetControllerDescription() const override { return NSLOCTEXT("ProjectJHandContactGoals","Title","Project J Hand Contact Goals"); }
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
