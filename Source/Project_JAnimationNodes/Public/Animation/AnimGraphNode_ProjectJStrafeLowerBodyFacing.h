#pragma once

#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_ProjectJStrafeLowerBodyFacing.h"
#include "AnimGraphNode_ProjectJStrafeLowerBodyFacing.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UAnimGraphNode_ProjectJStrafeLowerBodyFacing : public UAnimGraphNode_Base
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_ProjectJStrafeLowerBodyFacing Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override { return TEXT("Project J"); }
};
