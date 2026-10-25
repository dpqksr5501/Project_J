#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Chooser.h"
#include "AnimGraphNode_ControlRig.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "ControlRig.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimationRuntime.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

bool UProject_JPlayerLocomotionLibrary::RebaseCompatiblePhysics(UPhysicsAsset* Physics, USkeletalMesh* Previous,
 USkeletalMesh* Target, const TMap<FName,FName>& BoneMap, FTransform SourceToTarget, bool PreserveBoneLocalFrames)
{
 if ((GEditor && GEditor->PlayWorld) || !Physics || !Previous || !Target || Previous==Target || Physics==Previous->GetPhysicsAsset() ||
     !SourceToTarget.IsValid() || !SourceToTarget.GetScale3D().Equals(FVector::OneVector,.0001) ||
     (PreserveBoneLocalFrames && !SourceToTarget.Equals(FTransform::Identity,.0001))) return false;
 const auto& A=Previous->GetRefSkeleton(); const auto& B=Target->GetRefSkeleton();
 TArray<FTransform> Old, New;
 FAnimationRuntime::FillUpComponentSpaceTransforms(A,A.GetRefBonePose(),Old);
 FAnimationRuntime::FillUpComponentSpaceTransforms(B,B.GetRefBonePose(),New);
 TMap<FName,FTransform> Deltas; TMap<FName,FName> Names;
 auto Resolve=[&](FName Name)
 {
  if (Deltas.Contains(Name)) return true;
  const FName Mapped=BoneMap.Contains(Name) ? BoneMap.FindChecked(Name) : Name;
  const int32 Before=A.FindBoneIndex(Name), After=B.FindBoneIndex(Mapped);
  if (Before==INDEX_NONE || After==INDEX_NONE) return false;
  Names.Add(Name,Mapped); Deltas.Add(Name,PreserveBoneLocalFrames ? FTransform::Identity :
   (Old[Before]*SourceToTarget).GetRelativeTransform(New[After]));
  return true;
 };
 for (USkeletalBodySetup* Body:Physics->SkeletalBodySetups)
 {
  if (!Body || !Resolve(Body->BoneName)) return false;
  const auto& G=Body->AggGeom;
  if (G.GetElementCount()!=G.SphereElems.Num()+G.BoxElems.Num()+G.SphylElems.Num()+G.ConvexElems.Num()+G.TaperedCapsuleElems.Num()) return false;
 }
 for (UPhysicsConstraintTemplate* Constraint:Physics->ConstraintSetup)
  if (!Constraint || !Resolve(Constraint->DefaultInstance.ConstraintBone1) || !Resolve(Constraint->DefaultInstance.ConstraintBone2)) return false;
 Physics->Modify();
 for (USkeletalBodySetup* Body:Physics->SkeletalBodySetups)
 {
  Body->Modify(); const FTransform Delta=Deltas.FindChecked(Body->BoneName);
  auto Rebase=[&](auto& Shapes){for (auto& Shape:Shapes) Shape.SetTransform(Shape.GetTransform()*Delta);};
  Rebase(Body->AggGeom.SphereElems); Rebase(Body->AggGeom.BoxElems); Rebase(Body->AggGeom.SphylElems);
  Rebase(Body->AggGeom.ConvexElems); Rebase(Body->AggGeom.TaperedCapsuleElems);
  Body->BoneName=Names.FindChecked(Body->BoneName); Body->InvalidatePhysicsData();
 }
 for (UPhysicsConstraintTemplate* Constraint:Physics->ConstraintSetup)
 {
  Constraint->Modify(); auto& C=Constraint->DefaultInstance;
  C.SetRefFrame(EConstraintFrame::Frame1,C.GetRefFrame(EConstraintFrame::Frame1)*Deltas.FindChecked(C.ConstraintBone1));
  C.SetRefFrame(EConstraintFrame::Frame2,C.GetRefFrame(EConstraintFrame::Frame2)*Deltas.FindChecked(C.ConstraintBone2));
  C.ConstraintBone1=Names.FindChecked(C.ConstraintBone1); C.ConstraintBone2=Names.FindChecked(C.ConstraintBone2);
  if (const FName* Mapped=BoneMap.Find(C.JointName)) C.JointName=*Mapped;
 }
 Physics->UpdateBodySetupIndexMap(); Physics->UpdateBoundsBodiesArray();
 Physics->SetPreviewMesh(Target); Physics->PostEditChange(); Physics->MarkPackageDirty();
 return true;
}

bool UProject_JPlayerLocomotionLibrary::PrepareCompatibleBody(USkeletalMesh* Source, USkeletalMesh* Target)
{
 if ((GEditor && GEditor->PlayWorld) || !Source || !Target || !Source->GetSkeleton() || !Target->GetSkeleton()) return false;
 auto* From=Source->GetSkeleton(); auto* To=Target->GetSkeleton();
 const auto& A=Source->GetRefSkeleton(); const auto& B=Target->GetRefSkeleton();
 if (A.GetNum()==0 || B.GetNum()==0 || A.GetBoneName(0)!=B.GetBoneName(0)) return false;
 int32 Common=0;
 for (int32 Index=1;Index<B.GetNum();++Index)
 {
  const int32 Other=A.FindBoneIndex(B.GetBoneName(Index)); if (Other==INDEX_NONE) continue;
  const int32 Parent=B.GetParentIndex(Index), OtherParent=A.GetParentIndex(Other);
  if (Parent==INDEX_NONE || OtherParent==INDEX_NONE || B.GetBoneName(Parent)!=A.GetBoneName(OtherParent)) return false;
  ++Common;
 }
 if (!Common) return false;
 if (From==To) return true;
 // Preflight the complete contract before creating anything. Existing body tuning
 // is not silently overwritten by this authoring operation.
 for (const UBlendProfile* Profile:From->BlendProfiles) if (Profile)
  if (const auto* Existing=To->GetBlendProfile(Profile->GetFName()))
  {
   if (Existing->GetMode()!=Profile->GetMode()) return false;
   for (int32 Index=0;Index<Profile->GetNumBlendEntries();++Index)
   {
    const auto& Entry=Profile->GetEntry(Index);
    if (B.FindBoneIndex(Entry.BoneReference.BoneName)!=INDEX_NONE &&
        !FMath::IsNearlyEqual(Existing->GetBoneBlendScale(Entry.BoneReference.BoneName),Entry.BlendScale)) return false;
   }
  }
 for (const auto& Group:From->GetSlotGroups()) for (FName Slot:Group.SlotNames)
  if (To->ContainsSlotName(Slot) && To->GetSlotGroupName(Slot)!=Group.GroupName) return false;
 To->Modify();
 for (const UBlendProfile* Profile:From->BlendProfiles) if (Profile && !To->GetBlendProfile(Profile->GetFName()))
 {
  auto* Copy=To->CreateNewBlendProfile(Profile->GetFName()); if (!Copy) return false;
  Copy->Mode=Profile->GetMode();
  for (int32 Index=0;Index<Profile->GetNumBlendEntries();++Index)
  {
   const auto& Entry=Profile->GetEntry(Index);
   if (B.FindBoneIndex(Entry.BoneReference.BoneName)!=INDEX_NONE)
    Copy->SetBoneBlendScale(Entry.BoneReference.BoneName,Entry.BlendScale,false,true);
  }
 }
 for (const auto& Group:From->GetSlotGroups()) for (FName Slot:Group.SlotNames)
  if (!To->ContainsSlotName(Slot)) To->SetSlotGroupName(Slot,Group.GroupName);
 To->AddCompatibleSkeleton(From);
 To->MarkPackageDirty();
 return true;
}

bool UProject_JPlayerLocomotionLibrary::UseCompatibleBodyInput(UAnimBlueprint* Blueprint, USkeletalMesh* Target)
{
 if ((GEditor && GEditor->PlayWorld) || !Blueprint || !Target || !Target->GetSkeleton()) return false;
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
 UAnimGraphNode_RetargetPoseFromMesh* Retarget=nullptr;
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
 {
  if (Cast<UAnimGraphNode_LinkedInputPose>(Node)) return false;
  if (auto* Candidate=Cast<UAnimGraphNode_RetargetPoseFromMesh>(Node))
  { if (Retarget) return false; Retarget=Candidate; }
 }
 if (!Retarget) return false;
 auto* Output=Retarget->FindPin(TEXT("Pose"),EGPD_Output);
 if (!Output || Output->LinkedTo.IsEmpty()) return false;
 auto* Graph=Retarget->GetGraph(); const auto Consumers=Output->LinkedTo;
 Blueprint->Modify(); Graph->Modify();
 auto* Input=NewObject<UAnimGraphNode_LinkedInputPose>(Graph,NAME_None,RF_Transactional);
 Input->Node.Name=TEXT("InputPose"); Input->Node.Graph=TEXT("AnimGraph");
 Graph->AddNode(Input,false,false); Input->CreateNewGuid(); Input->PostPlacedNewNode(); Input->AllocateDefaultPins();
 Input->NodePosX=Retarget->NodePosX; Input->NodePosY=Retarget->NodePosY;
 auto* Pose=Input->FindPin(TEXT("Pose"),EGPD_Output);
 if (!Pose) { FBlueprintEditorUtils::RemoveNode(Blueprint,Input,true); return false; }
 for (auto* Consumer:Consumers) { Consumer->BreakLinkTo(Output); Pose->MakeLinkTo(Consumer); }
 FBlueprintEditorUtils::RemoveNode(Blueprint,Retarget,true);
 Blueprint->TargetSkeleton=Target->GetSkeleton(); Blueprint->SetPreviewMesh(Target);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
 FKismetEditorUtilities::CompileBlueprint(Blueprint);
 Blueprint->MarkPackageDirty();
 return Blueprint->Status!=BS_Error;
}

bool UProject_JPlayerLocomotionLibrary::ConfigureCompatibleFoundation(UAnimBlueprint* Target,USkeletalMesh* Mesh,UChooserTable* Ground,UBlueprint* FootRig)
{
 if(!Target || !Mesh || !Mesh->GetSkeleton() || !Ground || !FootRig || !FootRig->GeneratedClass ||
    !FootRig->GeneratedClass->IsChildOf(UControlRig::StaticClass()) || !Target->TargetSkeleton)return false;
 USkeleton* Previous=Target->TargetSkeleton;USkeleton* Skeleton=Mesh->GetSkeleton();
 const auto& Ref=Mesh->GetRefSkeleton();
 for(const TCHAR* Bone:{TEXT("root"),TEXT("pelvis"),TEXT("spine_05"),TEXT("hand_l"),TEXT("hand_r"),
     TEXT("foot_l"),TEXT("foot_r"),TEXT("thigh_l"),TEXT("thigh_r"),TEXT("ik_foot_root"),TEXT("ik_foot_l"),TEXT("ik_foot_r")})
  if(Ref.FindBoneIndex(Bone)==INDEX_NONE)return false;
 TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);int32 Rigs=0;
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)Rigs+=Cast<UAnimGraphNode_ControlRig>(Node)!=nullptr;
 if(Rigs!=1)return false;
 const auto* DefaultRigProperty=FindFProperty<FStructProperty>(FAnimNode_ControlRig::StaticStruct(),TEXT("DefaultControlRigAssetReference"));
 if(!DefaultRigProperty || DefaultRigProperty->Struct!=FControlRigAssetStrongReference::StaticStruct())return false;
 Target->Modify();Skeleton->Modify();
 // Conversion does not establish pose compatibility. Separate compatible bodies
 // require explicit authoring validation; source UEFN clips are retargeted offline.
 // Slot/group ownership follows the actual montage contract, not bone indices.
 for(const auto& Group:Previous->GetSlotGroups())for(FName Slot:Group.SlotNames)
 {
  // Existing Manny montage groups remain authoritative for UE5 combat assets.
  if(!Skeleton->ContainsSlotName(Slot))Skeleton->SetSlotGroupName(Slot,Group.GroupName);
 }
 TMap<UObject*,UObject*> Remap;
 if(Previous!=Skeleton)for(const UBlendProfile* Profile:Previous->BlendProfiles)
 {
  const FName Name(*FString::Printf(TEXT("Player_%s"),*Profile->GetName()));
  auto* Copy=Skeleton->GetBlendProfile(Name);if(!Copy)Copy=Skeleton->CreateNewBlendProfile(Name);
  if(!Copy)return false;
  Copy->Mode=Profile->GetMode();
  for(int32 Index=0;Index<Profile->GetNumBlendEntries();++Index)
  {
   const auto& Entry=Profile->GetEntry(Index);
   if(Skeleton->GetReferenceSkeleton().FindBoneIndex(Entry.BoneReference.BoneName)!=INDEX_NONE)
    Copy->SetBoneBlendScale(Entry.BoneReference.BoneName,Entry.BlendScale,false,true);
  }
  Remap.Add(const_cast<UBlendProfile*>(Profile),Copy);
 }
 if(!Remap.IsEmpty())
 {
  FArchiveReplaceObjectRef<UObject> ReplaceGraph(Target,Remap,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
  FArchiveReplaceObjectRef<UObject> ReplaceChooser(Ground,Remap,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
 }
 if(Previous!=Skeleton && RemapLocomotionBlendProfileNames(Ground,Previous,Skeleton)<0)return false;
 Target->TargetSkeleton=Skeleton;Target->SetPreviewMesh(Mesh);
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
 {
  if(auto* Rig=Cast<UAnimGraphNode_ControlRig>(Node))
  {
   Rig->Node.SetControlRigClass(TSubclassOf<UControlRig>(FootRig->GeneratedClass.Get()));
   // SetControlRigClass is a runtime override: it deliberately retains the previous
   // default. OnInitializeAnimInstance clears that override when a default exists.
   // Authored migration must update both reflected references, or play still uses
   // the UEFN hierarchy despite the graph displaying the Manny rig.
   *DefaultRigProperty->ContainerPtrToValuePtr<FControlRigAssetStrongReference>(&Rig->Node)=Rig->Node.GetControlRigAssetReference();
  }
  if(auto* Warp=Cast<UAnimGraphNode_OrientationWarping>(Node))
   Warp->Node.SpineBones.RemoveAll([&](const FBoneReference& Bone){return Ref.FindBoneIndex(Bone.BoneName)==INDEX_NONE;});
  if(auto* History=Cast<UAnimGraphNode_PoseSearchHistoryCollector>(Node))
  {
   const auto* Property=FindFProperty<FStructProperty>(History->GetClass(),TEXT("Node"));if(!Property)return false;
   const auto* Data=Property->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector>(History);
   for(const auto& Bone:Data->CollectedBones)if(Ref.FindBoneIndex(Bone.BoneName)==INDEX_NONE)return false;
  }
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
 FBlueprintEditorUtils::RefreshAllNodes(Target);FKismetEditorUtilities::CompileBlueprint(Target);
 if(Target->Status==BS_Error || !Target->GeneratedClass)return false;
 auto* Defaults=Cast<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject());if(!Defaults)return false;
 Defaults->GroundAnimationChooser=Ground;
 Target->MarkPackageDirty();Skeleton->MarkPackageDirty();Ground->MarkPackageDirty();
 return true;
}
