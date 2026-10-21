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
