#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JStrafeUpperBodyFacingEditorLibrary.h"
#include "Animation/AnimGraphNode_ProjectJStrafeUpperBodyFacing.h"
#include "Animation/AnimGraphNode_ProjectJStrafeLowerBodyFacing.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/Project_JFacingFailureTestSchema.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJFacingInstallerAtomicTest, "ProjectJ.Continuation.FacingInstallerPreflightAtomic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJFacingInstallerAtomicTest::RunTest(const FString&)
{
    TStrongObjectPtr<UAnimBlueprint> Master(LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master")));
    if (!TestNotNull(TEXT("Authored master asset is available"), Master.Get())) return false;
    TStrongObjectPtr<UAnimBlueprint> Clone(DuplicateObject<UAnimBlueprint>(Master.Get(), GetTransientPackage()));
    if (!TestNotNull(TEXT("Only a transient graph is changed"), Clone.Get())) return false;
    TestFalse(TEXT("Public entry remains restricted to the actual master path"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerBodyInMasterForReview(Clone.Get()));
    UEdGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Clone->FunctionGraphs) if (Candidate && Candidate->GetFName() == TEXT("AnimGraph")) Graph = Candidate;
    if (!TestNotNull(TEXT("Actual cloned AnimGraph"), Graph)) return false;
    UEdGraphPin* HistoryPose = nullptr; UEdGraphPin* Destination = nullptr;
    TArray<UEdGraphNode*> FacingNodes;
    for (UEdGraphNode* Node : Graph->Nodes) {
        if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_PoseSearchHistoryCollector")) HistoryPose = Node->FindPin(TEXT("Pose"), EGPD_Output);
        if (Cast<UAnimGraphNode_ProjectJStrafeUpperBodyFacing>(Node) || Cast<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Node)) FacingNodes.Add(Node);
    }
    if (!TestNotNull(TEXT("History output"), HistoryPose) || !TestEqual(TEXT("Single history output route"), HistoryPose->LinkedTo.Num(), 1)) return false;
    Destination = HistoryPose->LinkedTo[0];
    while (FacingNodes.Contains(Destination->GetOwningNode())) {
        auto* Output = Destination->GetOwningNode()->FindPin(TEXT("Pose"), EGPD_Output);
        if (!TestNotNull(TEXT("Facing output"), Output) || !TestEqual(TEXT("Single facing route"), Output->LinkedTo.Num(), 1)) return false;
        Destination = Output->LinkedTo[0];
    }
    for (auto* Node : FacingNodes) { Node->BreakAllNodeLinks(); Graph->RemoveNode(Node); }
    HistoryPose->BreakAllPinLinks(); HistoryPose->MakeLinkTo(Destination);
    for (int32 Index = 0; Index < 2; ++Index) {
        auto* Duplicate = NewObject<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>(Graph);
        Duplicate->CreateNewGuid(); Graph->AddNode(Duplicate, false, false); Duplicate->AllocateDefaultPins();
    }
    const int32 Before = Graph->Nodes.Num();
    const bool DirtyBefore = Clone->GetOutermost()->IsDirty();
    TestFalse(TEXT("Duplicate lower nodes reject the whole requested install"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerGraphForReview(Clone.Get()));
    TestEqual(TEXT("Rejected lower install must not leave an added upper node"), Graph->Nodes.Num(), Before);
    TestEqual(TEXT("Rejected install retains the exact original edge"), HistoryPose->LinkedTo.Num(), 1);
    TestTrue(TEXT("Rejected install retains the destination"), HistoryPose->LinkedTo.Contains(Destination));
    TestEqual(TEXT("Rejected preflight leaves initial package dirtiness"), Clone->GetOutermost()->IsDirty(), DirtyBefore);
    TArray<UEdGraphNode*> Duplicates;
    for (UEdGraphNode* Node : Graph->Nodes) if (Node->IsA<UAnimGraphNode_ProjectJStrafeLowerBodyFacing>()) Duplicates.Add(Node);
    for (auto* Node : Duplicates) { Node->BreakAllNodeLinks(); Graph->RemoveNode(Node); }
    const auto OriginalSchema = Graph->Schema;
    Graph->Schema = UProject_JFacingFailureTestSchema::StaticClass();
    const int32 BeforePartialFailure = Graph->Nodes.Num();
    const bool DirtyBeforePartialFailure = Clone->GetOutermost()->IsDirty();
    TestFalse(TEXT("A schema's second connection can reject the operation"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerGraphForReview(Clone.Get()));
    TestEqual(TEXT("Both new facing nodes are returned on failure"), Graph->Nodes.Num(), BeforePartialFailure);
    TestEqual(TEXT("Partial failure restores the single old route"), HistoryPose->LinkedTo.Num(), 1);
    TestTrue(TEXT("Partial failure restores the exact old destination"), HistoryPose->LinkedTo.Contains(Destination));
    TestEqual(TEXT("Partial failure returns initial dirtiness"), Clone->GetOutermost()->IsDirty(), DirtyBeforePartialFailure);
    Graph->Schema = OriginalSchema;
    TestTrue(TEXT("A valid lower+upper chain compiles in one operation"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerGraphForReview(Clone.Get()));
    const int32 InstalledCount = Graph->Nodes.Num();
    TestEqual(TEXT("Exactly the two required nodes were added"), InstalledCount, BeforePartialFailure + 2);
    TestTrue(TEXT("An installed valid chain is idempotent"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerGraphForReview(Clone.Get()));
    TestEqual(TEXT("Idempotence allocates no extra nodes"), Graph->Nodes.Num(), InstalledCount);
    TestNotNull(TEXT("The compiled class exists"), Clone->GeneratedClass.Get());
    return true;
}
#endif
