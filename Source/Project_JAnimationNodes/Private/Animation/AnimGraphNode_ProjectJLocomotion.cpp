#include "Animation/AnimGraphNode_ProjectJLocomotion.h"

FText UAnimGraphNode_ProjectJLocomotion::GetNodeTitle(ENodeTitleType::Type) const
{ return NSLOCTEXT("ProjectJ", "UnifiedLocomotionTitle", "Project J Unified Locomotion"); }
FText UAnimGraphNode_ProjectJLocomotion::GetTooltipText() const
{ return NSLOCTEXT("ProjectJ", "UnifiedLocomotionTooltip", "State Controller clips and Motion Matching share one playback stack and per-player correction graph."); }
FText UAnimGraphNode_ProjectJLocomotion::GetMenuCategory() const
{ return NSLOCTEXT("ProjectJ", "UnifiedLocomotionCategory", "Animation|Project J"); }
