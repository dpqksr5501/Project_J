#pragma once

#include "AnimGraphNode_BlendStack.h"
#include "Animation/AnimNode_ProjectJLocomotion.h"
#include "AnimGraphNode_ProjectJLocomotion.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UAnimGraphNode_ProjectJLocomotion : public UAnimGraphNode_BlendStack_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_ProjectJLocomotion Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;
	virtual UScriptStruct* GetTimePropertyStruct() const override { return FAnimNode_ProjectJLocomotion::StaticStruct(); }
protected:
	virtual FAnimNode_BlendStack_Standalone* GetBlendStackNode() const override
	{ return const_cast<FAnimNode_ProjectJLocomotion*>(&Node); }
};
