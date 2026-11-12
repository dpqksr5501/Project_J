#pragma once

#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/Project_JAnimNode_WingInheritance.h"
#include "Project_JAnimGraphNode_WingInheritance.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UProject_JAnimGraphNode_WingInheritance : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Settings")
    FProject_JAnimNode_WingInheritance Node;

    virtual FText GetNodeTitle(ENodeTitleType::Type) const override { return GetControllerDescription(); }
    virtual FText GetTooltipText() const override
    {
        return NSLOCTEXT("ProjectJWingInheritance", "Tooltip", "Distribute torso rotation along Winger's authored wing chains, with delayed outer joints.");
    }

protected:
    virtual FText GetControllerDescription() const override
    {
        return NSLOCTEXT("ProjectJWingInheritance", "Title", "Winger Wing Inheritance");
    }
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
