#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimGraphNode_ProjectJLocomotion.h"
#include "Animation/AnimGraphNode_ProjectJOneShotHandoff.h"
#include "AnimGraphNode_BlendStack.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUnifiedLocomotionGraph,
	"ProjectJ.Animation.A_Prepare.UnifiedLocomotionGraph", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJUnifiedLocomotionGraph::RunTest(const FString&)
{
	auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Production Master"), BP)) return false;
	TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
	for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Unified = Cast<UAnimGraphNode_ProjectJLocomotion>(Node))
		{
			AddInfo(TEXT("Master already contains the unified playback node; read-only audit."));
			bool bValid = TestTrue(TEXT("Unified output has one consumer"), Unified->FindPin(TEXT("Pose")) &&
				Unified->FindPin(TEXT("Pose"))->LinkedTo.Num() == 1);
			bValid &= TestTrue(TEXT("Unified database retains native supplier"), Unified->FindPin(TEXT("Database")) &&
				Unified->FindPin(TEXT("Database"))->LinkedTo.Num() == 1);
			const auto Samples = Unified->GetSubGraphs();
			bValid &= TestEqual(TEXT("One shared sample graph"), Samples.Num(), 1);
			int32 Routes = 0;
			if (!Samples.IsEmpty()) for (UEdGraphNode* Sample : Samples[0]->Nodes)
			{
				if (!Cast<UAnimGraphNode_BlendListByBool>(Sample)) continue;
				++Routes;
				for (const TCHAR* PinName : {TEXT("BlendTime_0"), TEXT("BlendTime_1")})
				{
					const auto* Pin = Sample->FindPin(PinName);
					bValid &= TestTrue(TEXT("Correction routing has zero duration and no time supplier"),
						Pin && Pin->LinkedTo.IsEmpty() && FMath::IsNearlyZero(FCString::Atof(*Pin->DefaultValue)));
				}
				for (const TCHAR* PinName : {TEXT("BlendPose_0"), TEXT("BlendPose_1"), TEXT("bActiveValue"), TEXT("Pose")})
					bValid &= TestTrue(TEXT("Correction routing inputs and output are connected"),
						Sample->FindPin(PinName) && Sample->FindPin(PinName)->LinkedTo.Num() == 1);
			}
			bValid &= TestEqual(TEXT("One processing selector, no timed animation handoff"), Routes, 1);
			for (UEdGraphNode* Top : Unified->GetGraph()->Nodes)
				bValid &= TestFalse(TEXT("No separate external playback or Handoff remains"),
					Cast<UAnimGraphNode_BlendStack>(Top) || Cast<UAnimGraphNode_ProjectJOneShotHandoff>(Top));
			return bValid;
		}
	}
	AddError(TEXT("Production Master must contain the unified playback node."));
	return false;
}
#endif
