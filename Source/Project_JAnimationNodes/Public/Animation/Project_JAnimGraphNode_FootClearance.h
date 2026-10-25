#pragma once
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/Project_JAnimNode_FootClearance.h"
#include "Project_JAnimGraphNode_FootClearance.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UProject_JAnimGraphNode_FootClearance : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,Category="Settings") FProject_JAnimNode_FootClearance Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override {return GetControllerDescription();}
	virtual FText GetTooltipText() const override {return NSLOCTEXT("ProjectJFootClearance","Tooltip","Minimal leg IK correction from active mesh sole samples and ground planes. No render-vertex skinning at runtime.");}
protected:
	virtual FText GetControllerDescription() const override {return NSLOCTEXT("ProjectJFootClearance","Title","Project J Sole Clearance");}
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override {return &Node;}
};
