#pragma once

#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_ProjectJStrafeUpperBodyFacing.h"
#include "AnimGraphNode_ProjectJStrafeUpperBodyFacing.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UAnimGraphNode_ProjectJStrafeUpperBodyFacing : public UAnimGraphNode_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_ProjectJStrafeUpperBodyFacing Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override { return TEXT("Project J"); }
};
