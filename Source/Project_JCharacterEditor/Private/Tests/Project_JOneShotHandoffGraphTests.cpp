#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimGraphNode_ProjectJOneShotHandoff.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotHandoffGraphTest, "ProjectJ.Animation.A_Prepare.OneShotReturnGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotHandoffGraphTest::RunTest(const FString&)
{
	auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Production Master"), BP)) return false;
	TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
	UAnimGraphNode_ProjectJOneShotHandoff* Handoff = nullptr;
	for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (auto* Found = Cast<UAnimGraphNode_ProjectJOneShotHandoff>(Node)) Handoff = Found;
	}
	if (!TestNotNull(TEXT("Production uses the explicit handoff node"), Handoff)) return false;
	if (!TestTrue(TEXT("Return policy is connected"), Handoff->FindPin(TEXT("BlendTime_0")) &&
		Handoff->FindPin(TEXT("BlendTime_0"))->LinkedTo.Num() == 1)) return false;
	return true;
}
#endif
