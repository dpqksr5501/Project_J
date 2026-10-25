#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JAnimGraphNode_HandContactGoals.h"
#include "Animation/Project_JAnimGraphNode_GuidedHandIK.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_IKRig.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "Editor.h"

bool UProject_JPlayerLocomotionLibrary::InstallDirectBodyContactIK(UAnimBlueprint* Blueprint,UIKRigDefinition* Rig)
{
 if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Blueprint->TargetSkeleton || !Rig || !Rig->GetPreviewMesh() ||
     Rig->GetPreviewMesh()->GetSkeleton()!=Blueprint->TargetSkeleton || !Blueprint->ParentClass ||
     !Blueprint->ParentClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass())) return false;
 UAnimGraphNode_LinkedInputPose* Input=nullptr;UProject_JAnimGraphNode_GuidedHandIK* Guides[2]={};
 TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
 {
  if(Node->IsA<UAnimGraphNode_RetargetPoseFromMesh>() || Node->IsA<UAnimGraphNode_IKRig>() || Node->IsA<UProject_JAnimGraphNode_HandContactGoals>()) return false;
  if(auto* Candidate=Cast<UAnimGraphNode_LinkedInputPose>(Node)){if(Input)return false;Input=Candidate;}
  if(auto* Guide=Cast<UProject_JAnimGraphNode_GuidedHandIK>(Node))
  {
   const auto Mode=Guide->Node.ArmDefinitionSource;
   if(Mode==EProject_JGuidedArmDefinitionSource::NodeSettings)return false;
   const int32 Side=Mode==EProject_JGuidedArmDefinitionSource::PrimaryBodyProfile?0:1;
   if(Guides[Side])return false;Guides[Side]=Guide;
   for(const TCHAR* Name:{TEXT("Alpha"),TEXT("bAlphaBoolEnabled"),TEXT("EffectorTransform"),TEXT("bUseBoneSpaceEffector"),TEXT("EffectorBoneSpaceTransform"),TEXT("EffectorSpaceBoneName")})
    if(Guide->HasBinding(Name))return false;
  }
 }
 if(!Input || !Guides[0] || !Guides[1] || Guides[0]->GetGraph()!=Input->GetGraph() || Guides[1]->GetGraph()!=Input->GetGraph())return false;
 auto* Output=Input->FindPin(TEXT("Pose"),EGPD_Output);if(!Output || Output->LinkedTo.IsEmpty())return false;
 const auto& Ref=Blueprint->TargetSkeleton->GetReferenceSkeleton();
 for(const TCHAR* Name:{TEXT("ik_hand_r"),TEXT("ik_hand_l"),TEXT("hand_r"),TEXT("hand_l"),TEXT("foot_r"),TEXT("foot_l")})
  if(Ref.FindBoneIndex(Name)==INDEX_NONE)return false;
 const auto& Goals=Rig->GetGoalArray();if(Goals.Num()!=4)return false;
 TSet<FName> GoalBones;
 for(const auto* Goal:Goals)
 {
  if(!Goal || GoalBones.Contains(Goal->BoneName))return false;
  GoalBones.Add(Goal->BoneName);
 }
 for(FName Name:{FName(TEXT("hand_r")),FName(TEXT("hand_l")),FName(TEXT("foot_r")),FName(TEXT("foot_l"))})if(!GoalBones.Contains(Name))return false;
 auto* Property=FindFProperty<FStructProperty>(UAnimGraphNode_IKRig::StaticClass(),TEXT("Node"));
 if(!Property || Property->Struct!=FAnimNode_IKRig::StaticStruct())return false;
 auto* Graph=Input->GetGraph();Blueprint->Modify();Graph->Modify();const auto Consumers=Output->LinkedTo;
 TArray<UEdGraphNode*> Added;
 auto Add=[&](UEdGraphNode* Node){Graph->AddNode(Node,false,false);Node->CreateNewGuid();Node->PostPlacedNewNode();Node->AllocateDefaultPins();Added.Add(Node);};
 auto Fail=[&](){for(auto* Node:Added)FBlueprintEditorUtils::RemoveNode(Blueprint,Node,true);return false;};
 auto* CS=NewObject<UAnimGraphNode_LocalToComponentSpace>(Graph,NAME_None,RF_Transactional);Add(CS);
 auto* Publish=NewObject<UProject_JAnimGraphNode_HandContactGoals>(Graph,NAME_None,RF_Transactional);
 Publish->Node.PrimaryGoalBone.BoneName=TEXT("ik_hand_r");Publish->Node.SecondaryGoalBone.BoneName=TEXT("ik_hand_l");Publish->Node.Alpha=1.f;Add(Publish);
 auto* Local=NewObject<UAnimGraphNode_ComponentToLocalSpace>(Graph,NAME_None,RF_Transactional);Add(Local);
 auto* Solve=NewObject<UAnimGraphNode_IKRig>(Graph,NAME_None,RF_Transactional);
 auto* Data=Property->ContainerPtrToValuePtr<FAnimNode_IKRig>(Solve);Data->RigDefinitionAsset=Rig;Data->AlphaInputType=EAnimAlphaInputType::Bool;
 for(const auto* Goal:Goals)
 {
  FIKRigGoal Value(Goal->GoalName,Goal->BoneName);Value.TransformSource=EIKRigGoalTransformSource::Bone;
  Value.SourceBone.BoneName=Goal->BoneName==TEXT("hand_r")?FName(TEXT("ik_hand_r")):Goal->BoneName==TEXT("hand_l")?FName(TEXT("ik_hand_l")):Goal->BoneName;
  Value.PositionAlpha=Value.RotationAlpha=1.f;Data->Goals.Add(Value);
 }
 Add(Solve);
 FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);auto* Active=Creator.CreateNode();
 Active->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,bHasActiveHandContact));Creator.Finalize();Added.Add(Active);
 const auto* Schema=Graph->GetSchema();auto Connect=[&](UEdGraphPin* A,UEdGraphPin* B){return A && B && Schema->TryCreateConnection(A,B);};
 if(!Connect(CS->FindPin(TEXT("ComponentPose"),EGPD_Output),Publish->FindPin(TEXT("ComponentPose"))) ||
    !Connect(Publish->FindPin(TEXT("Pose"),EGPD_Output),Local->FindPin(TEXT("ComponentPose"))) ||
    !Connect(Local->FindPin(TEXT("Pose"),EGPD_Output),Solve->FindPin(TEXT("Source"))) ||
    !Connect(Active->GetValuePin(),Solve->FindPin(TEXT("bAlphaBoolEnabled"))) || !Solve->FindPin(TEXT("Pose"),EGPD_Output))return Fail();
 TSet<UK2Node_VariableGet*> ReplacedVariables;
 for(int32 Side=0;Side<2;++Side)
 {
  auto* Guide=Guides[Side];Guide->Modify();
  for(const TCHAR* Name:{TEXT("Alpha"),TEXT("bAlphaBoolEnabled"),TEXT("EffectorTransform"),TEXT("bUseBoneSpaceEffector"),TEXT("EffectorBoneSpaceTransform"),TEXT("EffectorSpaceBoneName")})
   if(auto* Pin=Guide->FindPin(Name))
   {
    for(auto* Link:Pin->LinkedTo)if(auto* Get=Cast<UK2Node_VariableGet>(Link->GetOwningNode()))ReplacedVariables.Add(Get);
    Pin->BreakAllPinLinks();
   }
  Guide->Node.AlphaInputType=EAnimAlphaInputType::Bool;Guide->Node.bUseBoneSpaceEffector=true;
  Guide->Node.EffectorSpaceBoneName=Side==0?FName(TEXT("ik_hand_r")):FName(TEXT("ik_hand_l"));
  Guide->Node.EffectorBoneSpaceTransform=FTransform::Identity;
  for(auto& Pin:Guide->ShowPinForProperties)
   if(Pin.PropertyName==TEXT("bUseBoneSpaceEffector") || Pin.PropertyName==TEXT("EffectorSpaceBoneName") || Pin.PropertyName==TEXT("EffectorBoneSpaceTransform"))Pin.bShowPin=true;
  Guide->ReconstructNode();
  Guide->FindPinChecked(TEXT("bUseBoneSpaceEffector"))->DefaultValue=TEXT("true");
  Guide->FindPinChecked(TEXT("EffectorSpaceBoneName"))->DefaultValue=Guide->Node.EffectorSpaceBoneName.ToString();
  Guide->FindPinChecked(TEXT("EffectorBoneSpaceTransform"))->DefaultValue=TEXT("0,0,0|0,0,0|1,1,1");
  if(!Connect(Active->GetValuePin(),Guide->FindPin(TEXT("bAlphaBoolEnabled"))))return false;
 }
 for(auto* Variable:ReplacedVariables)if(Variable->GetValuePin()->LinkedTo.IsEmpty())FBlueprintEditorUtils::RemoveNode(Blueprint,Variable,true);
 for(auto* Consumer:Consumers){Consumer->BreakLinkTo(Output);Solve->FindPin(TEXT("Pose"),EGPD_Output)->MakeLinkTo(Consumer);}
 Output->MakeLinkTo(CS->FindPin(TEXT("LocalPose")));
 CS->NodePosX=Input->NodePosX+200;Publish->NodePosX=CS->NodePosX+220;Local->NodePosX=Publish->NodePosX+260;Solve->NodePosX=Local->NodePosX+220;
 CS->NodePosY=Publish->NodePosY=Local->NodePosY=Solve->NodePosY=Input->NodePosY;Active->NodePosX=Solve->NodePosX;Active->NodePosY=Solve->NodePosY+300;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
 Blueprint->MarkPackageDirty();return Blueprint->Status!=BS_Error;
}
