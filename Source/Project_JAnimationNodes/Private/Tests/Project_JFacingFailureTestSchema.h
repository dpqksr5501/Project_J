#pragma once
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "Animation/AnimGraphNode_ProjectJStrafeLowerBodyFacing.h"
#include "Project_JFacingFailureTestSchema.generated.h"

/** Private automation schema injects a failure after read-only connection preflight. */
UCLASS()
class UProject_JFacingFailureTestSchema : public UAnimationGraphSchema
{
	GENERATED_BODY()
public:
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override
	{
		if (A && A->Direction == EGPD_Output && A->GetOwningNode()->IsA<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>()) return false;
		return Super::TryCreateConnection(A, B);
	}
};
