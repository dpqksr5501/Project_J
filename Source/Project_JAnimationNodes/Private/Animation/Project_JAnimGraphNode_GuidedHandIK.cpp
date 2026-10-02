#include "Animation/Project_JAnimGraphNode_GuidedHandIK.h"

#define LOCTEXT_NAMESPACE "ProjectJGuidedHandIK"

FText UProject_JAnimGraphNode_GuidedHandIK::GetControllerDescription() const
{
	return LOCTEXT("Title", "Project J Guided Hand IK");
}

FText UProject_JAnimGraphNode_GuidedHandIK::GetNodeTitle(ENodeTitleType::Type) const
{
	return GetControllerDescription();
}

FText UProject_JAnimGraphNode_GuidedHandIK::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Corrects a hand to an already calibrated component-space WRIST target. Uses stable input-pose elbow guidance and actual limb lengths. Read arm roles/stability from the body profile, or set static bones. Explicit upper/forearm roles support intervening helpers while preserving their input segment-relative pose. Empty roles use immediate parents. No stretching or separate recovery clock.");
}

#undef LOCTEXT_NAMESPACE
