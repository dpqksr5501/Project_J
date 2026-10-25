#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_CopyBone.h"
#include "Animation/Project_JAnimGraphNode_FootClearance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBodyPoseMarkerContract,
 "ProjectJ.PlayerLocomotion.BodyPoseMarkerAuthoring",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJBodyPoseMarkerContract::RunTest(const FString&)
{
 auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/GreatSword/Meshes/SK_GreatSword_Complete.SK_GreatSword_Complete"));
 if(!TestNotNull(TEXT("Fixture skeleton"),Mesh))return false;
 auto* Factory=NewObject<UAnimBlueprintFactory>();Factory->TargetSkeleton=Mesh->GetSkeleton();
 Factory->ParentClass=UProject_JRetargetAnimInstance::StaticClass();
 auto* BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),GetTransientPackage(),
  MakeUniqueObjectName(GetTransientPackage(),UAnimBlueprint::StaticClass(),TEXT("MarkerContract")),RF_Transient,nullptr,GWarn));
 if(!TestNotNull(TEXT("Isolated body blueprint"),BP))return false;
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);UAnimGraphNode_Root* Root=nullptr;
 for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(auto* R=Cast<UAnimGraphNode_Root>(N))Root=R;
 if(!TestNotNull(TEXT("Animation root"),Root))return false;
 auto* Graph=Root->GetGraph();
 auto Add=[Graph](UEdGraphNode* Node){Graph->AddNode(Node,false,false);Node->CreateNewGuid();Node->PostPlacedNewNode();Node->AllocateDefaultPins();};
 auto* Input=NewObject<UAnimGraphNode_LinkedInputPose>(Graph);Input->Node.Name=TEXT("InputPose");Input->Node.Graph=TEXT("AnimGraph");Add(Input);
 auto* CS=NewObject<UAnimGraphNode_LocalToComponentSpace>(Graph);Add(CS);
 auto* Sole=NewObject<UProject_JAnimGraphNode_FootClearance>(Graph);Add(Sole);
 auto* Local=NewObject<UAnimGraphNode_ComponentToLocalSpace>(Graph);Add(Local);
 auto Connect=[this,Graph](UEdGraphPin* A,UEdGraphPin* B)
 {
  if(!TestNotNull(TEXT("Fixture output pin"),A) || !TestNotNull(TEXT("Fixture input pin"),B))return false;
  return Graph->GetSchema()->TryCreateConnection(A,B);
 };
 if(!Connect(Input->FindPin(TEXT("Pose"),EGPD_Output),CS->FindPin(TEXT("LocalPose"))) ||
    !Connect(CS->FindPin(TEXT("ComponentPose"),EGPD_Output),Sole->FindPin(TEXT("ComponentPose"))) ||
    !Connect(Sole->FindPin(TEXT("Pose"),EGPD_Output),Local->FindPin(TEXT("ComponentPose"))) ||
    !Connect(Local->FindPin(TEXT("Pose"),EGPD_Output),Root->FindPin(TEXT("Result"))))return false;
 auto Count=[Graph](){int32 N=0;for(UEdGraphNode* Node:Graph->Nodes)if(Node->NodeComment==TEXT("ProjectJ.BodyPoseMarkers"))++N;return N;};
 TestTrue(TEXT("Default preserves authored weapon reference"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP));
 TestEqual(TEXT("Only four final limb markers"),Count(),4);
 TestTrue(TEXT("Preserve is idempotent"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP));
 TestEqual(TEXT("No duplicate nodes"),Count(),4);
 auto* First=Cast<UAnimGraphNode_CopyBone>(Sole->FindPin(TEXT("Pose"),EGPD_Output)->LinkedTo[0]->GetOwningNode());
 TestNotNull(TEXT("Markers follow sole correction"),First);
 TestTrue(TEXT("Explicit wrist reference"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP,true));
 TestEqual(TEXT("Five derived markers"),Count(),5);
 TestTrue(TEXT("Derived is idempotent"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP,true));
 TestEqual(TEXT("No duplicate weapon writer"),Count(),5);
 TestTrue(TEXT("Switch back to authored weapon reference"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP,false));
 TestEqual(TEXT("Only owned gun marker is removed"),Count(),4);
 auto* Authored=NewObject<UAnimGraphNode_CopyBone>(Graph);Authored->Node.TargetBone.BoneName=TEXT("ik_hand_gun");Add(Authored);
 TestTrue(TEXT("Preserve leaves artist writer untouched"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP,false));
 TestFalse(TEXT("Derivation refuses competing artist writer"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(BP,true));
 TestEqual(TEXT("Conflict did not mutate chain"),Count(),4);
 auto* Master=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
 TestFalse(TEXT("Shared master is not a direct body postprocess"),UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(Master,true));
 return true;
}
#endif
