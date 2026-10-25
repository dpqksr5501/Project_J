#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "Animation/Project_JAnimGraphNode_FootClearance.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"

bool UProject_JPlayerLocomotionLibrary::InstallBodyPoseMarkers(UAnimBlueprint* Blueprint, bool RebuildWeaponReferenceFromHand)
{
 if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Blueprint->TargetSkeleton || !Blueprint->ParentClass ||
     !Blueprint->ParentClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass())) return false;
 const TPair<FName,FName> Pairs[]={{TEXT("hand_r"),TEXT("ik_hand_gun")},{TEXT("hand_l"),TEXT("ik_hand_l")},
  {TEXT("hand_r"),TEXT("ik_hand_r")},{TEXT("foot_l"),TEXT("ik_foot_l")},{TEXT("foot_r"),TEXT("ik_foot_r")}};
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs); UAnimGraphNode_LinkedInputPose* InputPose=nullptr;
 TMap<FName,UAnimGraphNode_CopyBone*> Owned;
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
 {
  if(Node->IsA<UAnimGraphNode_RetargetPoseFromMesh>())return false;
  if (auto* Copy=Cast<UAnimGraphNode_CopyBone>(Node))
   for (const auto& Pair:Pairs) if (Copy->Node.TargetBone.BoneName==Pair.Value)
   {
    const bool IsOwned=Copy->NodeComment==TEXT("ProjectJ.BodyPoseMarkers");
    // An authored weapon trajectory is a separate contract. Preserve its writer
    // unless explicitly asked to derive that reference from the input wrist.
    if (!IsOwned && Pair.Value==TEXT("ik_hand_gun") && !RebuildWeaponReferenceFromHand) continue;
    if (!IsOwned || Copy->Node.SourceBone.BoneName!=Pair.Key || Owned.Contains(Pair.Value) ||
        !Copy->Node.bCopyTranslation || !Copy->Node.bCopyRotation || !Copy->Node.bCopyScale ||
        Copy->Node.ControlSpace!=BCS_ComponentSpace || Copy->Node.Alpha!=1.f) return false;
    Owned.Add(Pair.Value,Copy);
   }
  if (auto* Candidate=Cast<UAnimGraphNode_LinkedInputPose>(Node))
  { if (InputPose) return false; InputPose=Candidate; }
 }
 if (!InputPose) return false;
 for (int32 I=RebuildWeaponReferenceFromHand?0:1;I<UE_ARRAY_COUNT(Pairs);++I)
  if (Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(Pairs[I].Key)==INDEX_NONE ||
      Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(Pairs[I].Value)==INDEX_NONE) return false;
 auto* Input=InputPose->FindPin(TEXT("Pose"),EGPD_Output); if (!Input || Input->LinkedTo.Num()!=1) return false;
 auto* CS=Cast<UAnimGraphNode_LocalToComponentSpace>(Input->LinkedTo[0]->GetOwningNode());
 if(!CS)return false;
 auto* Head=CS->FindPin(TEXT("ComponentPose"),EGPD_Output);
 if(!Head || Head->LinkedTo.Num()!=1)return false;
 // Sole correction must precede final-pose marker publication on repeated calls too.
 if(auto* Sole=Cast<UProject_JAnimGraphNode_FootClearance>(Head->LinkedTo[0]->GetOwningNode()))
 { Head=Sole->FindPin(TEXT("Pose"),EGPD_Output);if(!Head || Head->LinkedTo.Num()!=1)return false; }
 auto* Graph=InputPose->GetGraph();
 if(!Owned.IsEmpty())
 {
  if(Owned.Num()!=4 && Owned.Num()!=5)return false;
  for(int32 I=1;I<UE_ARRAY_COUNT(Pairs);++I)if(!Owned.Contains(Pairs[I].Value))return false;
  auto* Cursor=Head;TSet<UEdGraphNode*> Seen;
  while(Cursor && Cursor->LinkedTo.Num()==1)
  {
   auto* Node=Cast<UAnimGraphNode_CopyBone>(Cursor->LinkedTo[0]->GetOwningNode());
   if(!Node || !Owned.FindKey(Node))break;
   if(Seen.Contains(Node) || Node->GetGraph()!=Graph || Cursor->LinkedTo[0]->LinkedTo.Num()!=1)return false;
   Seen.Add(Node);Cursor=Node->FindPin(TEXT("Pose"),EGPD_Output);
  }
  if(Seen.Num()!=Owned.Num() || !Cursor || Cursor->LinkedTo.Num()!=1)return false;
  auto* Gun=Owned.FindRef(TEXT("ik_hand_gun"));
  if(bool(Gun)==RebuildWeaponReferenceFromHand)return true;
  if(Gun)
  {
   auto* In=Gun->FindPin(TEXT("ComponentPose"));auto* Out=Gun->FindPin(TEXT("Pose"),EGPD_Output);
   if(!In || !Out || In->LinkedTo.Num()!=1 || Out->LinkedTo.Num()!=1)return false;
   Blueprint->Modify();Graph->Modify();auto* Before=In->LinkedTo[0];auto* After=Out->LinkedTo[0];
   FBlueprintEditorUtils::RemoveNode(Blueprint,Gun,true);Before->MakeLinkTo(After);
   FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
   Blueprint->MarkPackageDirty();return Blueprint->Status!=BS_Error;
  }
 }
 auto* Next=Head->LinkedTo[0]; Blueprint->Modify(); Graph->Modify();
 auto Add=[Graph](UEdGraphNode* Node)
 { Graph->AddNode(Node,false,false); Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->AllocateDefaultPins(); };
 TArray<UEdGraphNode*> Added;
 auto Fail=[&](){for (auto* Node:Added)FBlueprintEditorUtils::RemoveNode(Blueprint,Node,true);return false;};
 UEdGraphPin* Tail=nullptr;UEdGraphPin* First=nullptr;
 int32 X=CS->NodePosX;
 // Existing four-marker chains only need the explicitly requested weapon reference.
 const int32 Begin=Owned.IsEmpty()?(RebuildWeaponReferenceFromHand?0:1):0;
 const int32 End=Owned.IsEmpty()?UE_ARRAY_COUNT(Pairs):1;
 for (int32 I=Begin;I<End;++I)
 {
  const auto& Pair=Pairs[I];auto* Copy=NewObject<UAnimGraphNode_CopyBone>(Graph,NAME_None,RF_Transactional);
  Copy->Node.SourceBone.BoneName=Pair.Key; Copy->Node.TargetBone.BoneName=Pair.Value;
  Copy->Node.bCopyTranslation=Copy->Node.bCopyRotation=Copy->Node.bCopyScale=true;
  Copy->Node.ControlSpace=BCS_ComponentSpace;Copy->Node.Alpha=1.f;
  Copy->NodeComment=TEXT("ProjectJ.BodyPoseMarkers"); Add(Copy);Added.Add(Copy);
  auto* In=Copy->FindPin(TEXT("ComponentPose"));auto* Out=Copy->FindPin(TEXT("Pose"),EGPD_Output);
  if (!In || !Out) return Fail();if(Tail)Tail->MakeLinkTo(In);else First=In;Tail=Out;
  Copy->NodePosX=X+=240; Copy->NodePosY=InputPose->NodePosY;
 }
 Head->BreakLinkTo(Next);Head->MakeLinkTo(First);Tail->MakeLinkTo(Next);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
 Blueprint->MarkPackageDirty();return Blueprint->Status!=BS_Error;
}
