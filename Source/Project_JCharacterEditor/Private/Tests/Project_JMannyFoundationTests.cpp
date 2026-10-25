#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

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
  const auto* InitialFromMesh=FindFProperty<FBoolProperty>(FAnimNode_ControlRig::StaticStruct(),TEXT("bSetRefPoseFromSkeleton"));
  TestTrue(TEXT("Shared foot solver initializes from the actual body mesh"),InitialFromMesh && InitialFromMesh->GetPropertyValue_InContainer(&Node));
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
 for(const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"),TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C"),TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")})
 {
  auto* Class=LoadClass<AProject_JMoverPawn>(nullptr,Path);if(!TestNotNull(TEXT("Production avatar"),Class))continue;
  const auto* Pawn=Class->GetDefaultObject<AProject_JMoverPawn>();const auto* Mesh=Pawn->GetMesh()->GetSkeletalMeshAsset();
  const auto* Body=Mesh ? Mesh->GetSkeleton() : nullptr;
  const bool Compatible=Body && (Body==Manny || Body->GetCompatibleSkeletons().ContainsByPredicate(
   [Manny](const TSoftObjectPtr<USkeleton>& Entry){return Entry.Get()==Manny;}));
  TestTrue(TEXT("Avatar uses Manny or an explicitly compatible body skeleton"),Compatible);
  if(Compatible && Body!=Manny)
  {
   const auto& A=Manny->GetReferenceSkeleton();const auto& B=Mesh->GetRefSkeleton();
   TestEqual(TEXT("Compatible body retains the canonical root"),B.GetBoneName(0),A.GetBoneName(0));
   for(int32 Index=1;Index<B.GetNum();++Index)
   {
    const int32 Other=A.FindBoneIndex(B.GetBoneName(Index));if(Other==INDEX_NONE)continue;
    TestEqual(TEXT("Compatible shared bone keeps its parent"),B.GetBoneName(B.GetParentIndex(Index)),A.GetBoneName(A.GetParentIndex(Other)));
   }
   for(const UBlendProfile* Profile:Manny->BlendProfiles)if(Profile)
   {
    const UBlendProfile* Copy=nullptr;
    for(const UBlendProfile* Candidate:Body->BlendProfiles)
     if(Candidate && Candidate->GetFName()==Profile->GetFName()){Copy=Candidate;break;}
    if(TestNotNull(TEXT("Compatible body has each exact blend profile name"),Copy))
     TestEqual(TEXT("Compatible body preserves blend profile mode"),Copy->GetMode(),Profile->GetMode());
   }
   for(const auto& Group:Manny->GetSlotGroups())for(FName Slot:Group.SlotNames)
    TestEqual(TEXT("Compatible body preserves each montage slot group"),Body->GetSlotGroupName(Slot),Group.GroupName);
  }
  TestEqual(TEXT("One shared master class"),Pawn->GetMesh()->GetAnimClass(),BP->GeneratedClass.Get());
 }
 for(const auto& Compatible:Manny->GetCompatibleSkeletons())
  TestFalse(TEXT("Canonical playback cannot bypass conversion through UEFN compatibility"),Compatible.ToSoftObjectPath().ToString().Contains(TEXT("UEFN_Mannequin")));
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
 if(!TestNotNull(TEXT("Canonical skeleton"),Manny))return false;
 FString Text;
 TSharedPtr<FJsonObject> Catalog;
 const FString Filename=FPaths::ProjectDir()/TEXT("Scripts/Authoring/Data/PlayerAnimationLibrary.json");
 if(!TestTrue(TEXT("Portable animation catalog exists"),FFileHelper::LoadFileToString(Text,*Filename)) ||
    !TestTrue(TEXT("Animation catalog parses"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Catalog)))return false;
 if(!TestEqual(TEXT("Supported animation catalog schema"),int32(Catalog->GetNumberField(TEXT("schema_version"))),1))return false;
 TMap<FString,TSharedPtr<FJsonObject>> Entries;
 int32 ExpectedSequences=0,ExpectedSpaces=0;
 for(const auto& Value:Catalog->GetArrayField(TEXT("entries")))
 {
  const auto Row=Value->AsObject();const FString Path=Row->GetStringField(TEXT("target_package"));
  TestFalse(TEXT("Catalog target is unique"),Entries.Contains(Path));Entries.Add(Path,Row);
  if(Row->GetStringField(TEXT("class"))==TEXT("AnimSequence"))++ExpectedSequences;else ++ExpectedSpaces;
 }
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/Game/Characters/Mannequins/Animations"),Assets,true);
 int32 Sequences=0,BlendSpaces=0;
 for(const auto& Data:Assets)
 {
  const FString Path=Data.PackageName.ToString();
  if(!Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/")) &&
     !Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/ProjectJump/")))continue;
  auto* Animation=Cast<UAnimationAsset>(Data.GetAsset());if(!Animation)continue;
  TestEqual(TEXT("Every converted asset uses Manny"),Animation->GetSkeleton(),Manny);
  const auto* Found=Entries.Find(Path);
  if(!TestTrue(TEXT("Every authored library asset has an import/provenance contract: ")+Path,Found!=nullptr))continue;
  const auto Row=*Found;
  if(auto* Sequence=Cast<UAnimSequence>(Animation))
  {
   ++Sequences;const auto Contract=Row->GetObjectField(TEXT("target_contract"));
   TestEqual(TEXT("Clip duration is preserved"),double(Sequence->GetPlayLength()),Contract->GetNumberField(TEXT("length")));
   TestEqual(TEXT("Additive semantics are preserved"),int32(Sequence->AdditiveAnimType),int32(Contract->GetNumberField(TEXT("additive_type"))));
   TestEqual(TEXT("Additive base mode is preserved"),int32(Sequence->RefPoseType),int32(Contract->GetNumberField(TEXT("ref_pose_type"))));
   TestEqual(TEXT("Root-motion extraction contract is preserved"),Sequence->bEnableRootMotion,Contract->GetBoolField(TEXT("enable_root_motion")));
   TestEqual(TEXT("Root-lock contract is preserved"),int32(Sequence->RootMotionRootLock),int32(Contract->GetNumberField(TEXT("root_lock"))));
   TestEqual(TEXT("Forced root lock is preserved"),Sequence->bForceRootLock,Contract->GetBoolField(TEXT("force_root_lock")));
   const auto& Notifies=Contract->GetArrayField(TEXT("notifies"));
   TestEqual(TEXT("Notify event count is preserved"),Sequence->Notifies.Num(),Notifies.Num());
   if(Sequence->RefPoseSeq)TestEqual(TEXT("Additive references remain native Manny"),Sequence->RefPoseSeq->GetSkeleton(),Manny);
   for(int32 Index=0;Index<FMath::Min(Sequence->Notifies.Num(),Notifies.Num());++Index)
   {
    const auto& A=Sequence->Notifies[Index];const auto B=Notifies[Index]->AsObject();
    TestEqual(TEXT("Notify name"),A.NotifyName.ToString(),B->GetStringField(TEXT("name")));
    TestEqual(TEXT("Notify trigger timing including authored offset"),double(A.GetTriggerTime()),B->GetNumberField(TEXT("time")));
    TestEqual(TEXT("Notify duration"),double(A.GetDuration()),B->GetNumberField(TEXT("duration")));
    TestEqual(TEXT("Notify server policy"),A.bTriggerOnDedicatedServer,B->GetBoolField(TEXT("server")));
    TestEqual(TEXT("Notify class"),A.Notify?A.Notify->GetClass()->GetPathName():FString(),B->GetStringField(TEXT("class")));
    TestEqual(TEXT("Notify-state class"),A.NotifyStateClass?A.NotifyStateClass->GetClass()->GetPathName():FString(),B->GetStringField(TEXT("state_class")));
   }
  }
  else if(auto* Space=Cast<UBlendSpace>(Animation))
  {
   ++BlendSpaces;
   for(const auto& Sample:Space->GetBlendSamples())TestTrue(TEXT("BlendSpace samples use converted Manny clips"),Sample.Animation && Sample.Animation->GetSkeleton()==Manny);
  }
 }
 TestTrue(TEXT("Original complete library remains represented"),ExpectedSequences>=448 && ExpectedSpaces>=10);
 TestEqual(TEXT("Complete cataloged sequence library"),Sequences,ExpectedSequences);
 TestEqual(TEXT("Complete cataloged BlendSpace library"),BlendSpaces,ExpectedSpaces);
 return true;
}
#endif
