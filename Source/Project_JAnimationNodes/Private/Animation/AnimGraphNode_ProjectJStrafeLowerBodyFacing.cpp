#include "Animation/AnimGraphNode_ProjectJStrafeLowerBodyFacing.h"

#define LOCTEXT_NAMESPACE "ProjectJStrafeLowerBodyFacing"
FText UAnimGraphNode_ProjectJStrafeLowerBodyFacing::GetNodeTitle(ENodeTitleType::Type) const
{
	return LOCTEXT("Title", "Project J Strafe Lower Body Facing");
}
FText UAnimGraphNode_ProjectJStrafeLowerBodyFacing::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "After Pose History and before Upper Body Facing. Bounded forward Strafe Run pelvis yaw with analytic leg constraints preserving existing foot transforms. Exact bypass for turns, montages, one-shots, OTM, unsupported rigs and excluded owners/LODs.");
}
#undef LOCTEXT_NAMESPACE
