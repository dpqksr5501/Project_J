#include "Animation/Project_JStrafeUpperBodyFacingEditorLibrary.h"
#include "Animation/AnimGraphNode_ProjectJStrafeUpperBodyFacing.h"
#include "Animation/AnimGraphNode_ProjectJStrafeLowerBodyFacing.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallInMasterForReview(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || Blueprint->GetPathName() != TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master")) return false;
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		if (Candidate && Candidate->GetFName() == TEXT("AnimGraph")) Graph = Candidate;
	if (!Graph) return false;
	UEdGraphNode* History = nullptr;
	UAnimGraphNode_ProjectJStrafeUpperBodyFacing* Existing = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector"))
		{
			if (History) return false;
			History = Node;
		}
		if (auto* Facing = Cast<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Node))
		{
			if (Existing) return false;
			Existing = Facing;
		}
	}
	auto* Pose = History ? History->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	if (!Pose || Pose->LinkedTo.Num() != 1) return false;
	if (Existing)
	{
		auto* Input = Existing->FindPin(TEXT("Source"), EGPD_Input);
		auto* Output = Existing->FindPin(TEXT("Pose"), EGPD_Output);
		bool bHistoryConnected = Pose->LinkedTo[0] == Input;
		if (auto* Lower = Cast<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Pose->LinkedTo[0]->GetOwningNode()))
		{
			const auto* LowerOutput = Lower->FindPin(TEXT("Pose"), EGPD_Output);
			bHistoryConnected = Pose->LinkedTo[0]->PinName == TEXT("Source") && LowerOutput &&
				LowerOutput->LinkedTo.Num() == 1 && LowerOutput->LinkedTo[0] == Input;
		}
		return bHistoryConnected && Output && Output->LinkedTo.Num() == 1 &&
			Output->LinkedTo[0]->PinName == TEXT("BlendPose_0") && Blueprint->Status != BS_Error;
	}
	auto* Destination = Pose->LinkedTo[0];
	if (Destination->PinName != TEXT("BlendPose_0") ||
		Destination->GetOwningNode()->GetClass()->GetFName() != TEXT("AnimGraphNode_BlendListByEnum")) return false;
	auto* Facing = NewObject<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Graph, NAME_None, RF_Transactional);
	Graph->Modify();
	Facing->CreateNewGuid();
	Graph->AddNode(Facing, false, false);
	Facing->PostPlacedNewNode();
	Facing->AllocateDefaultPins();
	Facing->NodePosX = History->NodePosX + 300;
	Facing->NodePosY = History->NodePosY;
	auto* Input = Facing->FindPin(TEXT("Source"), EGPD_Input);
	auto* Output = Facing->FindPin(TEXT("Pose"), EGPD_Output);
	const auto* Schema = Graph->GetSchema();
	bool bConnected = Input && Output && Schema;
	if (bConnected)
	{
		Pose->BreakLinkTo(Destination);
		bConnected = Schema->TryCreateConnection(Pose, Input) && Schema->TryCreateConnection(Output, Destination);
	}
	if (bConnected)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		bConnected = Blueprint->Status != BS_Error && Blueprint->GeneratedClass != nullptr;
	}
	if (!bConnected)
	{
		Facing->BreakAllNodeLinks();
		Graph->RemoveNode(Facing);
		if (!Pose->LinkedTo.Contains(Destination)) Pose->MakeLinkTo(Destination);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return false;
	}
	return true;
}

bool UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerBodyInMasterForReview(UAnimBlueprint* Blueprint)
{
	if (!InstallInMasterForReview(Blueprint)) return false;
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		if (Candidate && Candidate->GetFName() == TEXT("AnimGraph")) Graph = Candidate;
	if (!Graph) return false;
	UEdGraphNode* History = nullptr;
	UAnimGraphNode_ProjectJStrafeUpperBodyFacing* Upper = nullptr;
	UAnimGraphNode_ProjectJStrafeLowerBodyFacing* Existing = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector"))
		{ if (History) return false; History = Node; }
		if (auto* U = Cast<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Node))
		{ if (Upper) return false; Upper = U; }
		if (auto* L = Cast<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Node))
		{ if (Existing) return false; Existing = L; }
	}
	auto* Pose = History ? History->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	auto* Destination = Upper ? Upper->FindPin(TEXT("Source"), EGPD_Input) : nullptr;
	if (!Pose || !Destination || Pose->LinkedTo.Num() != 1) return false;
	if (Existing)
	{
		const auto* Input = Existing->FindPin(TEXT("Source"), EGPD_Input);
		const auto* Output = Existing->FindPin(TEXT("Pose"), EGPD_Output);
		return Pose->LinkedTo[0] == Input && Output && Output->LinkedTo.Num() == 1 &&
			Output->LinkedTo[0] == Destination && Blueprint->Status != BS_Error;
	}
	if (Pose->LinkedTo[0] != Destination) return false;
	Graph->Modify();
	auto* Lower = NewObject<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Graph, NAME_None, RF_Transactional);
	Lower->CreateNewGuid(); Graph->AddNode(Lower, false, false);
	Lower->PostPlacedNewNode(); Lower->AllocateDefaultPins();
	Lower->NodePosX = History->NodePosX + 150; Lower->NodePosY = History->NodePosY + 150;
	auto* Input = Lower->FindPin(TEXT("Source"), EGPD_Input);
	auto* Output = Lower->FindPin(TEXT("Pose"), EGPD_Output);
	const auto* Schema = Graph->GetSchema();
	bool bConnected = Input && Output && Schema;
	if (bConnected)
	{
		Pose->BreakLinkTo(Destination);
		bConnected = Schema->TryCreateConnection(Pose, Input) && Schema->TryCreateConnection(Output, Destination);
	}
	if (bConnected)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		bConnected = Blueprint->Status != BS_Error && Blueprint->GeneratedClass;
	}
	if (!bConnected)
	{
		Lower->BreakAllNodeLinks(); Graph->RemoveNode(Lower);
		if (!Pose->LinkedTo.Contains(Destination)) Pose->MakeLinkTo(Destination);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	return bConnected;
}
