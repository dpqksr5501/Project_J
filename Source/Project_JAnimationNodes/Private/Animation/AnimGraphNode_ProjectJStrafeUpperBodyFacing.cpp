#include "Animation/AnimGraphNode_ProjectJStrafeUpperBodyFacing.h"

#define LOCTEXT_NAMESPACE "ProjectJStrafeUpperBodyFacing"
FText UAnimGraphNode_ProjectJStrafeUpperBodyFacing::GetNodeTitle(ENodeTitleType::Type) const
{
	return LOCTEXT("Title", "Project J Strafe Upper Body Facing");
}
FText UAnimGraphNode_ProjectJStrafeUpperBodyFacing::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Local-pose presentation after Pose History. Reads a value-only Combat profile snapshot and corrects excess chest yaw across configured spine roles. Preserves root, legs and authored lean; forwards the source unchanged for excluded modes, owners and LODs. No search or previous-output feedback.");
}
#undef LOCTEXT_NAMESPACE
