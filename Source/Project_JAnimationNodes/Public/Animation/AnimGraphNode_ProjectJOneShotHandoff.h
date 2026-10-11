#pragma once
#include "AnimGraphNode_BlendListBase.h"
#include "Animation/AnimNode_ProjectJOneShotHandoff.h"
#include "AnimGraphNode_ProjectJOneShotHandoff.generated.h"

UCLASS()
class PROJECT_JANIMATIONNODES_API UAnimGraphNode_ProjectJOneShotHandoff : public UAnimGraphNode_BlendListBase
{
	GENERATED_BODY()
public:
	UAnimGraphNode_ProjectJOneShotHandoff();
	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_ProjectJOneShotHandoff Node;
	virtual FText GetNodeTitle(ENodeTitleType::Type) const override;
	virtual FText GetTooltipText() const override;
	virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const override;
};
