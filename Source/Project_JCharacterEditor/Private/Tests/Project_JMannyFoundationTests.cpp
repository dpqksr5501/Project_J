#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Chooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AnimGraphNode_ControlRig.h"
#include "EdGraph/EdGraph.h"
#include "UObject/UnrealType.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "AnimGraphNode_BlendStack.h"
#include "Animation/AnimNodeBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMannyFoundation,"ProjectJ.PlayerLocomotion.MannyFoundation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJMannyFoundation::RunTest(const FString&)
{
 auto* BP=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
 auto* Manny=LoadObject<USkeleton>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
 if(!TestNotNull(TEXT("Production master"),BP)||!TestNotNull(TEXT("Manny skeleton"),Manny))return false;
 TestEqual(TEXT("Master and converted locomotion use Manny"),BP->TargetSkeleton.Get(),Manny);
 const auto* DefaultRigProperty=FindFProperty<FStructProperty>(FAnimNode_ControlRig::StaticStruct(),TEXT("DefaultControlRigAssetReference"));
 if(!TestNotNull(TEXT("Control Rig initialization default contract"),DefaultRigProperty))return false;
 auto* ExpectedRig=LoadClass<UControlRig>(nullptr,TEXT("/Game/Animation_Logic/Rigs/CR_ProjectJ_Manny_FootPlacement.CR_ProjectJ_Manny_FootPlacement_C"));
 if(!TestNotNull(TEXT("Manny foot solver"),ExpectedRig))return false;
 const auto CheckRig=[&](const FAnimNode_ControlRig& Node)
 {
  TestEqual(TEXT("Authored foot rig uses Manny"),Node.GetControlRigAssetReference().GetRigClass(),ExpectedRig);
  const auto* Default=DefaultRigProperty->ContainerPtrToValuePtr<FControlRigAssetStrongReference>(&Node);
  TestEqual(TEXT("Initialization cannot revert to the UEFN foot hierarchy"),Default->GetRigClass(),ExpectedRig);
 };
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)if(const auto* Rig=Cast<UAnimGraphNode_ControlRig>(Node))CheckRig(Rig->Node);
 int32 CompiledRigs=0;
 for(TFieldIterator<FStructProperty> Property(BP->GeneratedClass);Property;++Property)
  if(Property->Struct==FAnimNode_ControlRig::StaticStruct())
  {++CompiledRigs;CheckRig(*Property->ContainerPtrToValuePtr<FAnimNode_ControlRig>(BP->GeneratedClass->GetDefaultObject()));}
 TestEqual(TEXT("One compiled foot solver was inspected"),CompiledRigs,1);
 for(const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player.BP_Player_C"),TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C"),TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")})
 {
  auto* Class=LoadClass<AProject_JMoverPawn>(nullptr,Path);if(!TestNotNull(TEXT("Production avatar"),Class))continue;
  const auto* Pawn=Class->GetDefaultObject<AProject_JMoverPawn>();const auto* Mesh=Pawn->GetMesh()->GetSkeletalMeshAsset();
  TestTrue(TEXT("Avatar evaluates on the canonical Manny skeleton"),Mesh && Mesh->GetSkeleton()==Manny);
  TestEqual(TEXT("One shared master class"),Pawn->GetMesh()->GetAnimClass(),BP->GeneratedClass.Get());
 }
 for(const TCHAR* Path:{TEXT("/Game/GASP58Native/Characters/UEFN_Mannequin/Animations/Run/M_Relaxed_Run_Loop_F.M_Relaxed_Run_Loop_F"),TEXT("/Game/Characters/UEFN_Mannequin/Animations/Jump/M_Neutral_Jump_F_Start_Stand_Lfoot.M_Neutral_Jump_F_Start_Stand_Lfoot")})
 {
  const auto* Sequence=LoadObject<UAnimSequence>(nullptr,Path);if(!TestNotNull(TEXT("Original locomotion sequence"),Sequence))continue;
  TestNotEqual(TEXT("UEFN source association stays intact"),Sequence->GetSkeleton(),Manny);
  TestFalse(TEXT("Retained UEFN sources cannot bypass canonical Manny conversion"),Manny->IsCompatibleForEditor(Sequence->GetSkeleton()));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMannySearchSpace,"ProjectJ.PlayerLocomotion.MannySearchSpace",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJMannySearchSpace::RunTest(const FString&)
{
 const auto* BP=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
 if(!BP || !BP->GeneratedClass)return false;
 const auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject());
 auto* Chooser=Defaults->GroundAnimationChooser.Get();if(!TestNotNull(TEXT("Project-owned ground chooser"),Chooser))return false;
 TestTrue(TEXT("Runtime no longer mutates the sample's authoring chooser"),Chooser->GetPathName().StartsWith(TEXT("/Game/Animation_Logic/Chooser/CHT_ProjectJ_Ground")));
 TArray<UObject*> Owned;GetObjectsWithOuter(Chooser,Owned,EGetObjectsFlags::IncludeNestedObjects);
 Owned.Add(LoadObject<UPoseSearchDatabase>(nullptr,TEXT("/Game/Animation_Logic/PoseSearch/PSD_Player_IdleBranches.PSD_Player_IdleBranches")));int32 Count=0;
 for(auto* Object:Owned)if(auto* DB=Cast<UPoseSearchDatabase>(Object))
 {
  ++Count;if(!TestNotNull(TEXT("Embedded database schema"),DB->Schema.Get()))continue;
  for(const auto& Role:DB->Schema->GetRoledSkeletons())
  {
   TestEqual(TEXT("Indexed pose and live pose use the same skeleton"),Role.Skeleton.Get(),BP->TargetSkeleton.Get());
   TestTrue(TEXT("Mirror metadata belongs to the same skeleton"),!Role.MirrorDataTable || Role.MirrorDataTable->Skeleton==BP->TargetSkeleton);
  }
  for(int32 Index=0;Index<DB->GetNumAnimationAssets();++Index)
   if(const auto* Entry=DB->GetDatabaseAnimationAsset(Index);Entry && Entry->IsEnabled())
   {
    TestTrue(TEXT("Every enabled candidate remains skeleton-compatible"),Entry->IsSkeletonCompatible(DB->Schema));
    const auto* Candidate=Cast<UAnimationAsset>(Entry->GetAnimationAsset());
    TestTrue(TEXT("Production search candidates are native Manny animations"),Candidate && Candidate->GetSkeleton()==BP->TargetSkeleton);
    TestTrue(TEXT("Production search excludes source banks and developer comparison clips"),Candidate && Candidate->GetPathName().StartsWith(TEXT("/Game/Characters/Mannequins/Animations/")));
   }
 }
 TestTrue(TEXT("Real embedded databases were inspected"),Count>=5);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMannyPlaybackBindings,"ProjectJ.PlayerLocomotion.MannyPlaybackBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJMannyPlaybackBindings::RunTest(const FString&)
{
 auto* Manny=LoadObject<USkeleton>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
 auto* Mirror=LoadObject<UMirrorDataTable>(nullptr,TEXT("/Game/Animation_Logic/PoseSearch/MDT_Player_Manny.MDT_Player_Manny"));
 if(!TestNotNull(TEXT("Canonical skeleton"),Manny)||!TestNotNull(TEXT("Manny playback mirror metadata"),Mirror))return false;
 TestEqual(TEXT("Playback mirroring uses the target skeleton"),Mirror->Skeleton.Get(),Manny);
 TCustomBoneIndexArray<FSkeletonPoseBoneIndex,FSkeletonPoseBoneIndex> Mapping;Mirror->FillMirrorBoneIndexes(Manny,Mapping);
 for(const TCHAR* Stem:{TEXT("thigh"),TEXT("calf"),TEXT("foot"),TEXT("ball"),TEXT("hand"),TEXT("ik_foot"),TEXT("ik_hand")})
 {
  const int32 Left=Manny->GetReferenceSkeleton().FindBoneIndex(FName(*(FString(Stem)+TEXT("_l"))));
  const int32 Right=Manny->GetReferenceSkeleton().FindBoneIndex(FName(*(FString(Stem)+TEXT("_r"))));
  if(TestTrue(TEXT("Required mirror bones exist"),Left!=INDEX_NONE && Right!=INDEX_NONE))
  {
   TestEqual(TEXT("Left mirror mapping"),Mapping[FSkeletonPoseBoneIndex(Left)].GetInt(),Right);
   TestEqual(TEXT("Right mirror mapping"),Mapping[FSkeletonPoseBoneIndex(Right)].GetInt(),Left);
  }
 }
 int32 Stacks=0,AuthoredStacks=0,References=0;
 for(const TCHAR* Path:{TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"),TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers.ABP_Greatsword_Layers"),TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.ABP_Greatsword_Woman_RunTIme")})
 {
  auto* BP=LoadObject<UAnimBlueprint>(nullptr,Path);if(!TestTrue(TEXT("Production animation class loads"),BP && BP->GeneratedClass && BP->TargetSkeleton))continue;
  TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
  for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)if(auto* Stack=Cast<UAnimGraphNode_BlendStack>(Node))
  {
   ++AuthoredStacks;
   const auto* Property=FindFProperty<FStructProperty>(Stack->GetClass(),TEXT("Node"));
   if(TestTrue(TEXT("Authored BlendStack node payload exists"),Property && Property->Struct==FAnimNode_BlendStack::StaticStruct()))
    TestEqual(TEXT("Authored BlendStack mirror table matches Manny"),Property->ContainerPtrToValuePtr<FAnimNode_BlendStack>(Stack)->MirrorDataTable.Get(),Mirror);
  }
  const FReferenceSkeleton& Skeleton=BP->TargetSkeleton->GetReferenceSkeleton();
  TFunction<void(const FProperty*,const void*)> Inspect;
  Inspect=[&](const FProperty* Property,const void* Address)
  {
   if(const auto* Struct=CastField<FStructProperty>(Property))
   {
    if(Struct->Struct==FBoneReference::StaticStruct())
    {
     const auto* Bone=static_cast<const FBoneReference*>(Address);
     if(!Bone->BoneName.IsNone())
     {++References;TestTrue(FString(Path)+TEXT(" compiled bone ")+Bone->BoneName.ToString(),Skeleton.FindBoneIndex(Bone->BoneName)!=INDEX_NONE);}
    }
    else for(TFieldIterator<FProperty> Field(Struct->Struct);Field;++Field)Inspect(*Field,Field->ContainerPtrToValuePtr<void>(Address));
   }
   else if(const auto* Array=CastField<FArrayProperty>(Property))
   {
    FScriptArrayHelper Values(Array,Address);
    for(int32 Index=0;Index<Values.Num();++Index)Inspect(Array->Inner,Values.GetRawPtr(Index));
   }
  };
  for(TFieldIterator<FStructProperty> Property(BP->GeneratedClass);Property;++Property)
   if(Property->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
   {
    const void* Node=Property->ContainerPtrToValuePtr<void>(BP->GeneratedClass->GetDefaultObject());Inspect(*Property,Node);
    if(Property->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct()))
    {++Stacks;TestEqual(TEXT("Compiled BlendStack mirror table matches Manny"),static_cast<const FAnimNode_BlendStack*>(Node)->MirrorDataTable.Get(),Mirror);}
   }
 }
 TestEqual(TEXT("Authored production BlendStack inspected"),AuthoredStacks,1);
 TestEqual(TEXT("Compiled production BlendStack inspected"),Stacks,AuthoredStacks);
 TestTrue(TEXT("Compiled skeletal control bindings inspected"),References>=10);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMannyConvertedLibrary,"ProjectJ.PlayerLocomotion.MannyConvertedLibrary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJMannyConvertedLibrary::RunTest(const FString&)
{
 auto* Manny=LoadObject<USkeleton>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"));
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/Game/Characters/Mannequins/Animations"),Assets,true);
 int32 Sequences=0,BlendSpaces=0;
 for(const auto& Data:Assets)
 {
  FString Path=Data.PackageName.ToString(),SourcePath;
  if(Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/")))SourcePath=Path.Replace(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/"),TEXT("/Game/GASP58Native/Characters/UEFN_Mannequin/Animations/"));
  else if(Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/ProjectJump/")))SourcePath=Path.Replace(TEXT("/Game/Characters/Mannequins/Animations/ProjectJump/"),TEXT("/Game/Characters/UEFN_Mannequin/Animations/"));
  else continue;
  auto* Animation=Cast<UAnimationAsset>(Data.GetAsset());if(!Animation)continue;
  TestEqual(TEXT("Every converted asset uses Manny"),Animation->GetSkeleton(),Manny);
  if(auto* Sequence=Cast<UAnimSequence>(Animation))
  {
   ++Sequences;auto* Source=LoadObject<UAnimSequence>(nullptr,*SourcePath);
   if(!TestNotNull(TEXT("Retained UEFN source"),Source))continue;
   TestNotEqual(TEXT("Retargeting did not reassign the original skeleton"),Source->GetSkeleton(),Manny);
   TestEqual(TEXT("Clip duration is preserved"),Sequence->GetPlayLength(),Source->GetPlayLength());
   TestEqual(TEXT("Additive semantics are preserved"),Sequence->AdditiveAnimType,Source->AdditiveAnimType);
   TestEqual(TEXT("Additive base mode is preserved"),Sequence->RefPoseType,Source->RefPoseType);
   TestEqual(TEXT("Root-motion extraction contract is preserved"),Sequence->bEnableRootMotion,Source->bEnableRootMotion);
   TestEqual(TEXT("Root-lock contract is preserved"),Sequence->RootMotionRootLock,Source->RootMotionRootLock);
   TestEqual(TEXT("Notify event count is preserved"),Sequence->Notifies.Num(),Source->Notifies.Num());
   if(Sequence->RefPoseSeq)TestEqual(TEXT("Additive references no longer depend on UEFN playback"),Sequence->RefPoseSeq->GetSkeleton(),Manny);
   for(int32 Index=0;Index<FMath::Min(Sequence->Notifies.Num(),Source->Notifies.Num());++Index)
   {
    const auto& A=Sequence->Notifies[Index];const auto& B=Source->Notifies[Index];
    TestEqual(TEXT("Notify name"),A.NotifyName,B.NotifyName);
    TestEqual(TEXT("Notify timing"),A.GetTime(),B.GetTime());
    TestEqual(TEXT("Notify duration"),A.GetDuration(),B.GetDuration());
    TestEqual(TEXT("Notify dedicated-server policy"),A.bTriggerOnDedicatedServer,B.bTriggerOnDedicatedServer);
    TestEqual(TEXT("Notify class"),A.Notify?A.Notify->GetClass():nullptr,B.Notify?B.Notify->GetClass():nullptr);
    TestEqual(TEXT("Notify-state class"),A.NotifyStateClass?A.NotifyStateClass->GetClass():nullptr,B.NotifyStateClass?B.NotifyStateClass->GetClass():nullptr);
   }
  }
  else if(auto* Space=Cast<UBlendSpace>(Animation))
  {
   ++BlendSpaces;
   for(const auto& Sample:Space->GetBlendSamples())TestTrue(TEXT("BlendSpace samples use converted Manny clips"),Sample.Animation && Sample.Animation->GetSkeleton()==Manny);
  }
 }
 TestEqual(TEXT("Complete converted sequence library"),Sequences,448);
 TestEqual(TEXT("Complete converted BlendSpace library"),BlendSpaces,10);
 return true;
}
#endif
