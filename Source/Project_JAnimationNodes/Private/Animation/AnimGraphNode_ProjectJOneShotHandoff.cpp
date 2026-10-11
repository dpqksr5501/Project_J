#include "Animation/AnimGraphNode_ProjectJOneShotHandoff.h"
UAnimGraphNode_ProjectJOneShotHandoff::UAnimGraphNode_ProjectJOneShotHandoff()
{
	Node.AddPose(); Node.AddPose();
}
FText UAnimGraphNode_ProjectJOneShotHandoff::GetNodeTitle(ENodeTitleType::Type) const
{
	return NSLOCTEXT("ProjectJ", "OneShotHandoffTitle", "One Shot → Motion Matching Handoff");
}
FText UAnimGraphNode_ProjectJOneShotHandoff::GetTooltipText() const
{
	return NSLOCTEXT("ProjectJ", "OneShotHandoffTooltip", "True: Motion Matching. False: external Blend Stack. Preserves outgoing Start/Stop/Land playback during a live return crossfade; TIP/Pivot keep inertialization.");
}
void UAnimGraphNode_ProjectJOneShotHandoff::CustomizePinData(UEdGraphPin* Pin, FName Property, int32 Index) const
{
	if (Index == INDEX_NONE || (Property != TEXT("BlendPose") && Property != TEXT("BlendTime"))) return;
	Pin->PinFriendlyName = Property == TEXT("BlendPose")
		? (Index == 0 ? NSLOCTEXT("ProjectJ", "HandoffMMPose", "True: Motion Matching Pose") : NSLOCTEXT("ProjectJ", "HandoffShotPose", "False: One Shot Pose"))
		: (Index == 0 ? NSLOCTEXT("ProjectJ", "HandoffReturnTime", "Return Blend Time") : NSLOCTEXT("ProjectJ", "HandoffEntryTime", "Entry Blend Time"));
}
