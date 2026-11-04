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
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimNode_LinkedAnimLayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Project_JNPCCharacter.h"
#include "Combat/Project_JAttackDefinition.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Variable.h"
#include "GameFramework/Character.h"

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
 for(const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"),TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"),TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C")})
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
 for(const TCHAR* Path:{TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"),TEXT("/Game/Animation_Logic/ABPs/Spellblade/ABP_Spellblade_Layers.ABP_Spellblade_Layers"),TEXT("/Game/Animation_Logic/ABPs/Spellblade/ABP_Spellblade_Woman_RunTIme.ABP_Spellblade_Woman_RunTIme")})
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCAuthoredAnimationPolicy138,
 "ProjectJ.NPCGameplay.AuthoredNonMotionMatchingAnimationPolicy", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNPCAuthoredAnimationPolicy138::RunTest(const FString&)
{
 const auto Receipt=MakeShared<FJsonObject>();Receipt->SetNumberField(TEXT("schema"),1);
 Receipt->SetStringField(TEXT("scope"),TEXT("selected_npc_candidate_compiled_animation_metadata"));
 Receipt->SetBoolField(TEXT("graph_behavior_executed"),false);Receipt->SetBoolField(TEXT("live_instances_observed"),false);
 Receipt->SetBoolField(TEXT("runtime_link_overrides_proven_absent"),false);
 FString Output=FPaths::ProjectSavedDir()/TEXT("ArchitectureBatch138NPCExecution/AnimationPolicy138.json");
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJNPCAnimationPolicyOutput="),Output);
 bool bReady=true,bCandidateReady=true,bCurrentFixtureReady=false,bCurrentScope=false;bool* ScopeReady=&bCandidateReady;TArray<FString> Unknown;TArray<TSharedPtr<FJsonValue>> ClassRows,PackageRows;
 TArray<TStrongObjectPtr<UObject>> Keep;TMap<UPackage*,bool> Dirty;
 auto Require=[&](bool Condition,const FString& Message)
 {
  if(!TestTrue(Message,Condition)){bReady=false;if(ScopeReady)*ScopeReady=false;Unknown.Add(Message);}return Condition;
 };
 auto Pin=[&](UObject* Object)
 {
  if(!Object)return;Keep.Emplace(Object);auto* Package=Object->GetOutermost();
  if(!Dirty.Contains(Package)){Dirty.Add(Package,Package->IsDirty());Require(!Package->IsDirty(),TEXT("Selected metadata package is clean after loading: ")+Package->GetName());}
 };
 ON_SCOPE_EXIT
 {
  bool bDirtyUnchanged=true;
  for(const auto& Entry:Dirty)
  {
   const bool bSame=Entry.Key->IsDirty()==Entry.Value;bDirtyUnchanged &= bSame;
   Require(bSame,TEXT("Observation preserves selected package dirty state: ")+Entry.Key->GetName());
   const auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("package"),Entry.Key->GetName());Row->SetBoolField(TEXT("dirty_after_load"),Entry.Value);Row->SetBoolField(TEXT("dirty_after_read"),Entry.Key->IsDirty());PackageRows.Add(MakeShared<FJsonValueObject>(Row));
  }
  if(!bDirtyUnchanged){bCandidateReady=false;bCurrentFixtureReady=false;}
  Receipt->SetBoolField(TEXT("candidate_ready"),bCandidateReady);Receipt->SetBoolField(TEXT("current_fixture_ready"),bCurrentFixtureReady);
  Receipt->SetBoolField(TEXT("metadata_success"),bReady);Receipt->SetBoolField(TEXT("dirty_unchanged"),bDirtyUnchanged);
  Receipt->SetArrayField(TEXT("classes"),ClassRows);Receipt->SetArrayField(TEXT("selected_packages"),PackageRows);
  TArray<TSharedPtr<FJsonValue>> Reasons;for(const auto& Reason:Unknown)Reasons.Add(MakeShared<FJsonValueString>(Reason));Receipt->SetArrayField(TEXT("unknown_reasons"),Reasons);
  FString Text;FJsonSerializer::Serialize(Receipt,TJsonWriterFactory<>::Create(&Text));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
  TestTrue(TEXT("Metadata receipt writes once without overwriting prior evidence"),FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting));
  AddInfo(TEXT("NPC animation metadata receipt: ")+Output);
 };
 auto* PlayerClass=LoadClass<UAnimInstance>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master_C"));
 auto* Candidate=LoadClass<UAnimInstance>(nullptr,TEXT("/Game/NPCs/Animation/ABP_NPC_Mover_Unarmed.ABP_NPC_Mover_Unarmed_C"));
 auto* NPCClass=LoadClass<AProject_JNPCCharacter>(nullptr,TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C"));
 auto* Blend=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));
 auto* Attack=LoadObject<UProject_JAttackDefinition>(nullptr,TEXT("/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.DA_Attack_NPC_PrototypeCleave"));
 for(UObject* Object:TArray<UObject*>{PlayerClass,Candidate,NPCClass,Blend,Attack})Pin(Object);
 if(!Require(PlayerClass && Candidate && NPCClass && Blend && Attack && Attack->Montage,TEXT("Selected player control, NPC Mover graph, NPC, BlendSpace and actual Cleave montage resolve")))return false;
 auto* Montage=Attack->Montage.Get();Pin(Montage);auto* NPC=NPCClass->GetDefaultObject<AProject_JNPCCharacter>();auto* Mesh=NPC ? NPC->GetMesh():nullptr;
 auto* MeshAsset=Mesh ? Mesh->GetSkeletalMeshAsset():nullptr;Pin(MeshAsset);
 if(!Require(Mesh && MeshAsset,TEXT("Selected NPC has an actual primary skeletal mesh")))return false;
 auto* NPCSkeleton=MeshAsset->GetSkeleton();Pin(NPCSkeleton);Pin(Blend->GetSkeleton());Pin(Montage->GetSkeleton());
 Receipt->SetStringField(TEXT("npc_class"),NPCClass->GetPathName());Receipt->SetStringField(TEXT("npc_mesh"),MeshAsset->GetPathName());Receipt->SetStringField(TEXT("npc_current_anim_class"),GetPathNameSafe(Mesh->GetAnimClass()));
 Receipt->SetStringField(TEXT("npc_effective_post_process_class"),GetPathNameSafe(Mesh->GetPostProcessAnimBPClassToBeUsed().Get()));
 Receipt->SetStringField(TEXT("npc_skeleton"),GetPathNameSafe(NPCSkeleton));Receipt->SetStringField(TEXT("blend_space"),Blend->GetPathName());Receipt->SetStringField(TEXT("blend_space_skeleton"),GetPathNameSafe(Blend->GetSkeleton()));
 Receipt->SetStringField(TEXT("attack_definition"),Attack->GetPathName());Receipt->SetStringField(TEXT("montage"),Montage->GetPathName());Receipt->SetStringField(TEXT("montage_skeleton"),GetPathNameSafe(Montage->GetSkeleton()));
 // Classic MotionMatching is not the only player PoseSearch consumer: the
 // native player driver also supplies the existing Chooser/BlendStack path.
 const auto IsHistoryNode=[](const UScriptStruct* Type){return Type && Type->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct());};
 const auto IsPoseSearchNode=[&](const UScriptStruct* Type){return Type && (Type->IsChildOf(FAnimNode_MotionMatching::StaticStruct()) || IsHistoryNode(Type));};
 const auto* PlayerInterface=IAnimClassInterface::GetFromClass(PlayerClass);int32 PlayerMM=0,PlayerPoseSearch=0,PlayerHistory=0;
 if(PlayerInterface && PlayerInterface->GetAnimNodeProperties().Num()<=4096)for(const auto* Property:PlayerInterface->GetAnimNodeProperties())if(Property && Property->Struct)
 {
  PlayerMM+=Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct());PlayerPoseSearch+=IsPoseSearchNode(Property->Struct);PlayerHistory+=IsHistoryNode(Property->Struct);
 }
 const bool bPlayerDriver=PlayerClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass());
 Receipt->SetStringField(TEXT("player_positive_control_class"),PlayerClass->GetPathName());Receipt->SetNumberField(TEXT("player_positive_control_mm_nodes"),PlayerMM);
 Receipt->SetNumberField(TEXT("player_positive_control_pose_search_nodes"),PlayerPoseSearch);Receipt->SetNumberField(TEXT("player_positive_control_history_nodes"),PlayerHistory);Receipt->SetBoolField(TEXT("player_positive_control_native_player_driver"),bPlayerDriver);
 Receipt->SetStringField(TEXT("detector_scope"),TEXT("Typed classic MotionMatching/history-collector nodes and native PlayerAnimInstance driver; compiled metadata, not runtime MM execution"));
 Require(bPlayerDriver && PlayerHistory>0,TEXT("Player master positively identifies the native player driver plus compiled PoseSearch history path, not execution"));
 TSet<UClass*> Seen;TSet<FName> Slots;int32 TotalNodes=0,TotalMM=0,TotalPoseSearch=0,TotalHistory=0,TotalPlayerDrivers=0;USkeleton* CandidateSkeleton=nullptr;bool bCandidateMain=false;
 TFunction<void(UClass*,bool)> Visit;
 Visit=[&](UClass* Class,bool bMain)
 {
  if(!Require(Class && Class->IsChildOf(UAnimInstance::StaticClass()),TEXT("Static linked implementation must be a concrete animation class")))return;
  if(Seen.Contains(Class))return;
  if(!Require(Seen.Num()<16,TEXT("Selected static class closure stays within16 classes")))return;
  Seen.Add(Class);Pin(Class);const auto* Interface=IAnimClassInterface::GetFromClass(Class);const auto* CDO=Class->GetDefaultObject<UAnimInstance>();Pin(const_cast<UAnimInstance*>(CDO));
  const auto Row=MakeShared<FJsonObject>();ClassRows.Add(MakeShared<FJsonValueObject>(Row));Row->SetStringField(TEXT("class"),Class->GetPathName());Row->SetStringField(TEXT("parent"),GetPathNameSafe(Class->GetSuperClass()));Row->SetBoolField(TEXT("primary_root"),bMain);Row->SetStringField(TEXT("source_scope"),bCurrentScope?TEXT("current_fixture"):TEXT("candidate"));
  if(!Require(Interface && CDO,TEXT("Each selected class exposes compiled animation metadata")))return;
  const auto& Nodes=Interface->GetAnimNodeProperties();TotalNodes+=Nodes.Num();Row->SetNumberField(TEXT("compiled_nodes"),Nodes.Num());
  if(!Require(!Nodes.IsEmpty() && TotalNodes<=4096,TEXT("Selected compiled node closure is nonempty and bounded4096")))return;
  auto* Skeleton=Interface->GetTargetSkeleton();Pin(Skeleton);Row->SetStringField(TEXT("target_skeleton"),GetPathNameSafe(Skeleton));
  Require(Skeleton && NPCSkeleton && (Skeleton==NPCSkeleton || Skeleton->IsCompatibleMesh(MeshAsset,true)),TEXT("Selected animation class accepts the actual NPC skeleton/bone hierarchy"));
  TArray<TSharedPtr<FJsonValue>> Functions,Linked,SlotRows;bool bMainImplemented=false;
  for(const auto& Function:Interface->GetAnimBlueprintFunctions())
  {
   const auto FunctionRow=MakeShared<FJsonObject>();FunctionRow->SetStringField(TEXT("name"),Function.Name.ToString());FunctionRow->SetStringField(TEXT("group"),Function.Group.ToString());FunctionRow->SetBoolField(TEXT("implemented"),Function.bImplemented);FunctionRow->SetBoolField(TEXT("output_property_present"),Function.OutputPoseNodeProperty!=nullptr);Functions.Add(MakeShared<FJsonValueObject>(FunctionRow));
   bMainImplemented |= Function.Name==FName(TEXT("AnimGraph")) && Function.bImplemented && Function.OutputPoseNodeProperty!=nullptr;
  }
  Row->SetArrayField(TEXT("functions"),Functions);Row->SetBoolField(TEXT("implemented_main_anim_graph"),bMainImplemented);
  if(bMain){CandidateSkeleton=Skeleton;bCandidateMain=bMainImplemented;Require(bMainImplemented,TEXT("Primary class has an implemented main AnimGraph; a layer-only class is not silently adopted"));}
  const bool bNativePlayerDriver=Class->IsChildOf(UProject_JPlayerAnimInstance::StaticClass());
  TMap<FString,int32> Types;TArray<UClass*> Next;int32 MM=0,PoseSearchNodes=0,HistoryNodes=0;
  for(const auto* Property:Nodes)
  {
   if(!Require(Property && Property->Struct,TEXT("Compiled node property has a public reflected struct type")))continue;
   ++Types.FindOrAdd(Property->Struct->GetPathName());
   if(Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))++MM;
   PoseSearchNodes+=IsPoseSearchNode(Property->Struct);HistoryNodes+=IsHistoryNode(Property->Struct);
   if(Property->Struct->IsChildOf(FAnimNode_Slot::StaticStruct()))
   {
    const auto* Node=Property->ContainerPtrToValuePtr<FAnimNode_Slot>(CDO);Slots.Add(Node->SlotName);SlotRows.Add(MakeShared<FJsonValueString>(Node->SlotName.ToString()));
   }
   if(Property->Struct->IsChildOf(FAnimNode_LinkedAnimGraph::StaticStruct()))
   {
    const auto* Node=Property->ContainerPtrToValuePtr<FAnimNode_LinkedAnimGraph>(CDO);UClass* Concrete=Node->InstanceClass.Get();const auto Link=MakeShared<FJsonObject>();Linked.Add(MakeShared<FJsonValueObject>(Link));
    Link->SetStringField(TEXT("property"),Property->GetName());Link->SetStringField(TEXT("instance_class"),GetPathNameSafe(Concrete));
    bool bKnownSelf=false;
    if(Property->Struct->IsChildOf(FAnimNode_LinkedAnimLayer::StaticStruct()))
    {
     const auto* Layer=Property->ContainerPtrToValuePtr<FAnimNode_LinkedAnimLayer>(CDO);Link->SetStringField(TEXT("interface"),GetPathNameSafe(Layer->Interface.Get()));Link->SetStringField(TEXT("layer"),Layer->Layer.ToString());
     if(!Concrete && !Layer->Interface)bKnownSelf=Interface->GetAnimBlueprintFunctions().ContainsByPredicate([&](const FAnimBlueprintFunction& F){return F.Name==Layer->Layer && F.bImplemented && F.OutputPoseNodeProperty;});
    }
    Link->SetBoolField(TEXT("known_self_layer"),bKnownSelf);
    if(Concrete)Next.AddUnique(Concrete);else Require(bKnownSelf,TEXT("Unresolved/dynamic linked animation implementation is unknown, not a non-MM result"));
   }
  }
  TotalMM+=MM;TotalPoseSearch+=PoseSearchNodes;TotalHistory+=HistoryNodes;TotalPlayerDrivers+=bNativePlayerDriver;
  Row->SetNumberField(TEXT("motion_matching_nodes"),MM);Row->SetNumberField(TEXT("pose_search_nodes"),PoseSearchNodes);Row->SetNumberField(TEXT("pose_search_history_nodes"),HistoryNodes);Row->SetBoolField(TEXT("native_player_driver"),bNativePlayerDriver);
  Require(MM==0,bCurrentScope?TEXT("Current fixture static implementation contains zero classic MM nodes"):TEXT("Candidate static implementation contains zero classic MM nodes"));
  Require(PoseSearchNodes==0,bCurrentScope?TEXT("Current fixture static implementation contains zero typed PoseSearch-family nodes"):TEXT("Candidate static implementation contains zero typed PoseSearch-family nodes"));
  Require(!bNativePlayerDriver,bCurrentScope?TEXT("Selected NPC fixture is not configured with the native player animation driver"):TEXT("Selected NPC candidate does not inherit the native player animation driver"));
  const auto TypeRows=MakeShared<FJsonObject>();for(const auto& Type:Types)TypeRows->SetNumberField(Type.Key,Type.Value);Row->SetObjectField(TEXT("node_struct_counts"),TypeRows);Row->SetArrayField(TEXT("slot_nodes"),SlotRows);Row->SetArrayField(TEXT("linked_nodes"),Linked);
  AddInfo(FString::Printf(TEXT("NPCAnimMetadata Class=%s Parent=%s Nodes=%d ClassicMM=%d PoseSearch=%d History=%d PlayerDriver=%d MainGraph=%d StaticLinks=%d"),*Class->GetPathName(),*GetPathNameSafe(Class->GetSuperClass()),Nodes.Num(),MM,PoseSearchNodes,HistoryNodes,bNativePlayerDriver,bMainImplemented,Next.Num()));
  for(auto* NextClass:Next)Visit(NextClass,false);
 };
 Visit(Candidate,true);Receipt->SetStringField(TEXT("candidate_class"),Candidate->GetPathName());Receipt->SetNumberField(TEXT("candidate_closure_classes"),Seen.Num());Receipt->SetNumberField(TEXT("candidate_closure_nodes"),TotalNodes);Receipt->SetNumberField(TEXT("candidate_closure_mm_nodes"),TotalMM);Receipt->SetNumberField(TEXT("candidate_closure_pose_search_nodes"),TotalPoseSearch);Receipt->SetNumberField(TEXT("candidate_closure_history_nodes"),TotalHistory);Receipt->SetNumberField(TEXT("candidate_closure_native_player_drivers"),TotalPlayerDrivers);Receipt->SetBoolField(TEXT("candidate_has_main_graph"),bCandidateMain);
 Require(CandidateSkeleton && Blend->GetSkeleton() && (CandidateSkeleton==Blend->GetSkeleton() || CandidateSkeleton->IsCompatibleForEditor(Blend->GetSkeleton())),TEXT("Existing Unarmed BlendSpace is compatible with the candidate skeleton"));
 Require(CandidateSkeleton && Montage->GetSkeleton() && (CandidateSkeleton==Montage->GetSkeleton() || CandidateSkeleton->IsCompatibleForEditor(Montage->GetSkeleton())),TEXT("Actual NPC Cleave montage is compatible with the candidate skeleton"));
 TArray<TSharedPtr<FJsonValue>> MontageSlots;Require(!Montage->SlotAnimTracks.IsEmpty(),TEXT("Actual NPC Cleave montage has authored slot tracks"));
 for(const auto& Track:Montage->SlotAnimTracks)
 {
  const bool bCompiled=Slots.Contains(Track.SlotName),bInSkeleton=CandidateSkeleton && CandidateSkeleton->ContainsSlotName(Track.SlotName);
  const auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("slot"),Track.SlotName.ToString());Row->SetBoolField(TEXT("present_in_compiled_candidate"),bCompiled);Row->SetBoolField(TEXT("present_in_candidate_skeleton"),bInSkeleton);Row->SetStringField(TEXT("candidate_group"),CandidateSkeleton?CandidateSkeleton->GetSlotGroupName(Track.SlotName).ToString():TEXT("None"));Row->SetStringField(TEXT("montage_group"),Montage->GetSkeleton()?Montage->GetSkeleton()->GetSlotGroupName(Track.SlotName).ToString():TEXT("None"));MontageSlots.Add(MakeShared<FJsonValueObject>(Row));
  Require(!Track.SlotName.IsNone() && bCompiled && bInSkeleton,TEXT("Every actual Cleave montage slot survives as a candidate compiled Slot node"));
 }
 Receipt->SetArrayField(TEXT("montage_slot_membership"),MontageSlots);

 // Selected authoring graph observation only. Do not change compiled-policy
 // readiness because a template's behavioral owner contract is different.
 const auto GraphEvidence=MakeShared<FJsonObject>();Receipt->SetObjectField(TEXT("candidate_editor_graph_observation"),GraphEvidence);
 GraphEvidence->SetStringField(TEXT("scope"),TEXT("Selected candidate public Editor graphs/pins; static wiring, not branch execution"));
 GraphEvidence->SetBoolField(TEXT("npc_class_is_character"),NPCClass->IsChildOf(ACharacter::StaticClass()));
 GraphEvidence->SetStringField(TEXT("npc_native_parent"),GetPathNameSafe(NPCClass->GetSuperClass()));
 bool bGraphObserved=true;int32 GraphNodeCount=0,GraphPinCount=0,GraphLinkCount=0,CharacterMovementCalls=0,AccelerationCalls=0,VelocityCalls=0,UnsatisfiedCasts=0,UnresolvedCalls=0;
 auto GraphCheck=[&](bool Condition,const FString& Reason)
 {
  if(!TestTrue(Reason,Condition)){bGraphObserved=false;bReady=false;Unknown.Add(TEXT("EditorGraphObservation: ")+Reason);}return Condition;
 };
 auto* CandidateBlueprint=UBlueprint::GetBlueprintFromClass(Candidate);Pin(CandidateBlueprint);
 GraphEvidence->SetStringField(TEXT("blueprint"),GetPathNameSafe(CandidateBlueprint));
 TArray<TSharedPtr<FJsonValue>> GraphRows;
 if(GraphCheck(CandidateBlueprint && CandidateBlueprint->GeneratedClass==Candidate,TEXT("Selected generated class retains its actual authoring Blueprint")))
 {
  // This is the installed public API also used by BlueprintEditorUtils templates.
  TArray<UEdGraph*> AuthoredGraphs;CandidateBlueprint->GetAllGraphs(AuthoredGraphs);
  GraphEvidence->SetNumberField(TEXT("graph_count"),AuthoredGraphs.Num());
  if(GraphCheck(!AuthoredGraphs.IsEmpty() && AuthoredGraphs.Num()<=32,TEXT("Selected candidate graphs are nonempty and bounded32")))
  for(auto* Graph:AuthoredGraphs)
  {
   if(!GraphCheck(IsValid(Graph),TEXT("Selected authoring graph remains live")))break;
   GraphNodeCount+=Graph->Nodes.Num();if(!GraphCheck(GraphNodeCount<=2048,TEXT("Selected authoring nodes remain bounded2048")))break;
   const auto GraphRow=MakeShared<FJsonObject>();GraphRow->SetStringField(TEXT("path"),Graph->GetPathName());GraphRow->SetStringField(TEXT("name"),Graph->GetName());GraphRow->SetNumberField(TEXT("node_count"),Graph->Nodes.Num());TArray<TSharedPtr<FJsonValue>> NodeRows;
   for(const auto& NodeObject:Graph->Nodes)
   {
    auto* Node=NodeObject.Get();if(!GraphCheck(IsValid(Node),TEXT("Selected graph node remains live")))break;
    GraphPinCount+=Node->Pins.Num();if(!GraphCheck(GraphPinCount<=8192,TEXT("Selected graph pin metadata remains bounded8192")))break;
    const auto NodeRow=MakeShared<FJsonObject>();NodeRow->SetStringField(TEXT("path"),Node->GetPathName());NodeRow->SetStringField(TEXT("name"),Node->GetName());NodeRow->SetStringField(TEXT("class"),Node->GetClass()->GetPathName());NodeRow->SetStringField(TEXT("guid"),Node->NodeGuid.ToString());
    if(const auto* Call=Cast<UK2Node_CallFunction>(Node))
    {
     const auto* Function=Call->GetTargetFunction();const FName FunctionName=Function?Function->GetFName():NAME_None;
     UnresolvedCalls+=Function==nullptr;NodeRow->SetStringField(TEXT("function"),FunctionName.ToString());NodeRow->SetBoolField(TEXT("function_resolved"),Function!=nullptr);NodeRow->SetStringField(TEXT("function_owner"),Function?GetPathNameSafe(Function->GetOuterUClass()):TEXT("None"));
     CharacterMovementCalls+=FunctionName==FName(TEXT("GetCharacterMovement"));AccelerationCalls+=FunctionName==FName(TEXT("GetCurrentAcceleration"));VelocityCalls+=FunctionName==FName(TEXT("GetVelocity"));
    }
    if(const auto* Variable=Cast<UK2Node_Variable>(Node))
    {
     const auto* Property=Variable->GetPropertyForVariable();NodeRow->SetStringField(TEXT("variable"),Variable->GetVarName().ToString());NodeRow->SetStringField(TEXT("variable_source_class"),GetPathNameSafe(Variable->GetVariableSourceClass()));NodeRow->SetStringField(TEXT("property"),Property?Property->GetPathName():TEXT("None"));
     if(const auto* ObjectProperty=CastField<FObjectPropertyBase>(Property))NodeRow->SetStringField(TEXT("property_object_class"),GetPathNameSafe(ObjectProperty->PropertyClass));
    }
    if(const auto* CastNode=Cast<UK2Node_DynamicCast>(Node))
    {
     const auto* SourcePin=CastNode->GetCastSourcePin();const auto* ResultPin=CastNode->GetCastResultPin();
     auto* TargetClass=ResultPin?Cast<UClass>(ResultPin->PinType.PinSubCategoryObject.Get()):nullptr;
     NodeRow->SetStringField(TEXT("cast_source_pin"),SourcePin?SourcePin->PinName.ToString():TEXT("None"));NodeRow->SetStringField(TEXT("cast_source_type"),SourcePin?GetPathNameSafe(SourcePin->PinType.PinSubCategoryObject.Get()):TEXT("None"));
     NodeRow->SetStringField(TEXT("cast_target_class"),GetPathNameSafe(TargetClass));NodeRow->SetBoolField(TEXT("npc_class_satisfies_cast_target"),TargetClass && NPCClass->IsChildOf(TargetClass));
     UnsatisfiedCasts+=TargetClass && !NPCClass->IsChildOf(TargetClass);
    }
    TArray<TSharedPtr<FJsonValue>> PinRows;
    for(const auto* PinValue:Node->Pins)
    {
     if(!GraphCheck(PinValue!=nullptr,TEXT("Selected public graph pin remains available")))break;
     GraphLinkCount+=PinValue->LinkedTo.Num();if(!GraphCheck(GraphLinkCount<=16384,TEXT("Selected pin connection metadata remains bounded16384")))break;
     const auto PinRow=MakeShared<FJsonObject>();PinRow->SetStringField(TEXT("name"),PinValue->PinName.ToString());PinRow->SetStringField(TEXT("category"),PinValue->PinType.PinCategory.ToString());PinRow->SetStringField(TEXT("type_object"),GetPathNameSafe(PinValue->PinType.PinSubCategoryObject.Get()));PinRow->SetStringField(TEXT("direction"),PinValue->Direction==EGPD_Input?TEXT("input"):TEXT("output"));TArray<TSharedPtr<FJsonValue>> Links;
     for(const auto* Linked:PinValue->LinkedTo)
     {
      if(!GraphCheck(Linked!=nullptr,TEXT("Selected public pin connection remains available")))break;
      const auto Link=MakeShared<FJsonObject>();Link->SetStringField(TEXT("node"),Linked->GetOwningNode()->GetPathName());Link->SetStringField(TEXT("pin"),Linked->PinName.ToString());Links.Add(MakeShared<FJsonValueObject>(Link));
     }
     PinRow->SetArrayField(TEXT("linked_to"),Links);PinRows.Add(MakeShared<FJsonValueObject>(PinRow));if(!bGraphObserved)break;
    }
    NodeRow->SetArrayField(TEXT("pins"),PinRows);NodeRows.Add(MakeShared<FJsonValueObject>(NodeRow));if(!bGraphObserved)break;
   }
   GraphRow->SetArrayField(TEXT("nodes"),NodeRows);GraphRows.Add(MakeShared<FJsonValueObject>(GraphRow));if(!bGraphObserved)break;
  }
 }
 GraphEvidence->SetBoolField(TEXT("observation_complete"),bGraphObserved);GraphEvidence->SetNumberField(TEXT("node_count"),GraphNodeCount);GraphEvidence->SetNumberField(TEXT("pin_count"),GraphPinCount);GraphEvidence->SetNumberField(TEXT("directed_link_count"),GraphLinkCount);GraphEvidence->SetNumberField(TEXT("get_character_movement_calls"),CharacterMovementCalls);GraphEvidence->SetNumberField(TEXT("get_current_acceleration_calls"),AccelerationCalls);GraphEvidence->SetNumberField(TEXT("get_velocity_calls"),VelocityCalls);GraphEvidence->SetNumberField(TEXT("unresolved_function_calls"),UnresolvedCalls);GraphEvidence->SetNumberField(TEXT("cast_targets_not_satisfied_by_selected_npc_class"),UnsatisfiedCasts);GraphEvidence->SetArrayField(TEXT("graphs"),GraphRows);
 AddInfo(FString::Printf(TEXT("NPCAnimEditorGraph NPCisCharacter=%d GraphNodes=%d CharacterMovementCalls=%d AccelerationCalls=%d VelocityCalls=%d UnsatisfiedCasts=%d; static wiring only"),NPCClass->IsChildOf(ACharacter::StaticClass()),GraphNodeCount,CharacterMovementCalls,AccelerationCalls,VelocityCalls,UnsatisfiedCasts));
 // Current fixture policy is a separate admission result. A legacy PlayerMaster
 // assignment must fail honestly even when the NPC Mover candidate is usable.
 bCurrentFixtureReady=true;ScopeReady=&bCurrentFixtureReady;bCurrentScope=true;
 Seen.Reset();Slots.Reset();TotalNodes=0;TotalMM=0;TotalPoseSearch=0;TotalHistory=0;TotalPlayerDrivers=0;CandidateSkeleton=nullptr;bCandidateMain=false;
 UClass* CurrentMain=Mesh->GetAnimClass();UClass* CurrentPost=Mesh->GetPostProcessAnimBPClassToBeUsed().Get();Receipt->SetBoolField(TEXT("current_fixture_matches_candidate"),CurrentMain==Candidate);
 if(Require(CurrentMain!=nullptr,TEXT("Current selected NPC primary mesh has an assigned main animation class")))Visit(CurrentMain,true);
 if(CurrentPost && CurrentPost!=CurrentMain)Visit(CurrentPost,false);
 Receipt->SetNumberField(TEXT("current_fixture_closure_classes"),Seen.Num());Receipt->SetNumberField(TEXT("current_fixture_closure_nodes"),TotalNodes);Receipt->SetNumberField(TEXT("current_fixture_closure_mm_nodes"),TotalMM);Receipt->SetNumberField(TEXT("current_fixture_closure_pose_search_nodes"),TotalPoseSearch);Receipt->SetNumberField(TEXT("current_fixture_closure_history_nodes"),TotalHistory);Receipt->SetNumberField(TEXT("current_fixture_closure_native_player_drivers"),TotalPlayerDrivers);
 Require(TotalNodes>0 && TotalMM==0 && TotalPoseSearch==0 && TotalPlayerDrivers==0,TEXT("Current selected NPC compiled main/post/static-linked graph excludes classic MM, typed PoseSearch nodes and the native player driver"));
 AddInfo(TEXT("Compiled candidate input/slot metadata only; no graph traversal execution, locomotion quality, montage output, natural notify or GAS playback proof."));
 return bReady;
}
#endif
