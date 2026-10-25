#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_RigidBody.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"

bool UProject_JPlayerLocomotionLibrary::BindBodySecondaryMotion(UAnimBlueprint* Blueprint)
{
 if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Blueprint->ParentClass ||
     !Blueprint->ParentClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass())) return false;
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs); TArray<UAnimGraphNode_RigidBody*> Bodies;
 const FName Enable=GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,bEnableSecondaryMotion);
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes) if (auto* Body=Cast<UAnimGraphNode_RigidBody>(Node))
 {
  for (const TCHAR* Name:{TEXT("Alpha"),TEXT("bAlphaBoolEnabled"),TEXT("AlphaCurveName")})
  {
   auto* Pin=Body->FindPin(Name); if (!Pin || Body->HasBinding(Name)) return false;
   if (Pin->LinkedTo.IsEmpty()) continue;
   const auto* Get=Pin->LinkedTo.Num()==1 ? Cast<UK2Node_VariableGet>(Pin->LinkedTo[0]->GetOwningNode()) : nullptr;
   if (FName(Name)!=TEXT("bAlphaBoolEnabled") || !Get || !Get->VariableReference.IsSelfContext() || Get->VariableReference.GetMemberName()!=Enable) return false;
  }
  Bodies.Add(Body);
 }
 if (Bodies.IsEmpty()) return false;
 Blueprint->Modify();
 for (auto* Body:Bodies)
 {
  Body->Modify();Body->Node.AlphaInputType=EAnimAlphaInputType::Bool;
  Body->ReconstructNode(); auto* Pin=Body->FindPin(TEXT("bAlphaBoolEnabled"));check(Pin);
  if (!Pin->LinkedTo.IsEmpty()) continue;
  auto* Graph=Body->GetGraph();Graph->Modify();
  FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);auto* Get=Creator.CreateNode();
  Get->VariableReference.SetSelfMember(Enable);Creator.Finalize();
  Get->NodePosX=Body->NodePosX-220;Get->NodePosY=Body->NodePosY+200;
  if (!Graph->GetSchema()->TryCreateConnection(Get->GetValuePin(),Pin))
  { FBlueprintEditorUtils::RemoveNode(Blueprint,Get,true);return false; }
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
 Blueprint->MarkPackageDirty();return Blueprint->Status!=BS_Error;
}
