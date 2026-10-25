#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_IKRig.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "Editor.h"

bool UProject_JPlayerLocomotionLibrary::InstallHandContactBodyIK(UAnimBlueprint* Blueprint, UIKRigDefinition* Rig)
{
 if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Rig || !Blueprint->ParentClass ||
     !Blueprint->ParentClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()) ||
     !Rig->GetPreviewMesh() || Rig->GetPreviewMesh()->GetSkeleton()!=Blueprint->TargetSkeleton) return false;
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
 UAnimGraphNode_RetargetPoseFromMesh* Retarget=nullptr;
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
 {
  if (Cast<UAnimGraphNode_IKRig>(Node)) return false;
  if (auto* Candidate=Cast<UAnimGraphNode_RetargetPoseFromMesh>(Node))
  { if (Retarget) return false; Retarget=Candidate; }
 }
 if (!Retarget) return false;
 const auto& Goals=Rig->GetGoalArray();
 if (Goals.Num()!=4) return false;
 TSet<FName> Bones;
 for (const auto* Goal:Goals)
 {
  if (!Goal || Bones.Contains(Goal->BoneName) || !Goal->bExposePosition || !Goal->bExposeRotation) return false;
  Bones.Add(Goal->BoneName);
 }
 for (FName Bone:{FName(TEXT("hand_r")),FName(TEXT("hand_l")),FName(TEXT("foot_r")),FName(TEXT("foot_l"))})
  if (!Bones.Contains(Bone)) return false;
 auto* Output=Retarget->FindPin(TEXT("Pose"),EGPD_Output);
 if (!Output || Output->LinkedTo.IsEmpty()) return false;
 auto* Property=FindFProperty<FStructProperty>(UAnimGraphNode_IKRig::StaticClass(),TEXT("Node"));
 auto* BreakFunction=UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BreakTransform"));
 if (!Property || Property->Struct!=FAnimNode_IKRig::StaticStruct() || !BreakFunction) return false;
 auto* Graph=Retarget->GetGraph(); Blueprint->Modify(); Graph->Modify();
 TArray<UEdGraphNode*> Added;
 auto* Solve=NewObject<UAnimGraphNode_IKRig>(Graph,NAME_None,RF_Transactional);
 auto* Data=Property->ContainerPtrToValuePtr<FAnimNode_IKRig>(Solve);
 Data->RigDefinitionAsset=Rig; Data->Alpha=1.f;
 for (const auto* Goal:Goals)
 {
  FIKRigGoal Value(Goal->GoalName,Goal->BoneName);
  Value.PositionSpace=Value.RotationSpace=EIKRigGoalSpace::Component;
  if (Goal->BoneName==TEXT("foot_l") || Goal->BoneName==TEXT("foot_r"))
  { Value.TransformSource=EIKRigGoalTransformSource::Bone; Value.SourceBone.BoneName=Goal->BoneName; }
  Data->Goals.Add(Value);
 }
 Graph->AddNode(Solve,false,false); Added.Add(Solve); Solve->CreateNewGuid(); Solve->PostPlacedNewNode(); Solve->AllocateDefaultPins();
 Solve->NodePosX=Retarget->NodePosX+350; Solve->NodePosY=Retarget->NodePosY;
 Solve->NodeComment=TEXT("Body follows active weapon contacts; input feet remain planted. Guided arm nodes retain bend-plane refinement.");
 auto Fail=[&](){for (auto* Node:Added) FBlueprintEditorUtils::RemoveNode(Blueprint,Node,true);return false;};
 const auto* Schema=Graph->GetSchema();
 for (const auto* Goal:Goals)
 {
  if (Goal->BoneName!=TEXT("hand_l") && Goal->BoneName!=TEXT("hand_r")) continue;
  const bool Right=Goal->BoneName==TEXT("hand_r");
  FGraphNodeCreator<UK2Node_VariableGet> TargetCreator(*Graph); auto* Target=TargetCreator.CreateNode();
  Target->VariableReference.SetSelfMember(Right?GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightWristTarget):GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,LeftWristTarget)); TargetCreator.Finalize(); Added.Add(Target);
  FGraphNodeCreator<UK2Node_VariableGet> AlphaCreator(*Graph); auto* Alpha=AlphaCreator.CreateNode();
  Alpha->VariableReference.SetSelfMember(Right?GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightGripAlpha):GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,LeftGripAlpha)); AlphaCreator.Finalize(); Added.Add(Alpha);
  FGraphNodeCreator<UK2Node_CallFunction> BreakCreator(*Graph); auto* Break=BreakCreator.CreateNode();
  Break->SetFromFunction(BreakFunction); BreakCreator.Finalize(); Added.Add(Break);
  const auto Pin=[&](const TCHAR* Field){return Solve->FindPin(FName(*FString::Printf(TEXT("%s_%s"),Field,*Goal->GoalName.ToString())));};
  auto Connect=[&](UEdGraphPin* A,UEdGraphPin* B){return A && B && Schema->TryCreateConnection(A,B);};
  if (!Connect(Target->GetValuePin(),Break->FindPin(TEXT("InTransform"))) ||
      !Connect(Break->FindPin(TEXT("Location")),Pin(TEXT("Position"))) ||
      !Connect(Break->FindPin(TEXT("Rotation")),Pin(TEXT("Rotation"))) ||
      !Connect(Alpha->GetValuePin(),Pin(TEXT("PositionAlpha"))) ||
      !Connect(Alpha->GetValuePin(),Pin(TEXT("RotationAlpha")))) return Fail();
  const int32 Y=Retarget->NodePosY+(Right?250:650);
  Target->NodePosX=Retarget->NodePosX-500; Target->NodePosY=Y;
  Break->NodePosX=Retarget->NodePosX-250; Break->NodePosY=Y;
  Alpha->NodePosX=Retarget->NodePosX; Alpha->NodePosY=Y+150;
 }
 auto* Input=Solve->FindPin(TEXT("Source")); auto* Result=Solve->FindPin(TEXT("Pose"),EGPD_Output);
 if (!Input || !Result) return Fail();
 const auto Consumers=Output->LinkedTo;
 for (auto* Consumer:Consumers) { Consumer->BreakLinkTo(Output); Result->MakeLinkTo(Consumer); }
 Output->MakeLinkTo(Input);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
 Blueprint->MarkPackageDirty(); return Blueprint->Status!=BS_Error;
}
