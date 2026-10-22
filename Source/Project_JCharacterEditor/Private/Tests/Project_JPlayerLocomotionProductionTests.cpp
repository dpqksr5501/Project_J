#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_BlendStack.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "Chooser.h"
#include "ChooserTypes.h"
#include "ChooserPropertyAccess.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/StrongObjectPtr.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Project_JAnimNotify_FoleyEvent.h"
#include "ObjectChooser_Asset.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "K2Node_AnimGetter.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "K2Node_VariableGet.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimNotifyQueue.h"
#include "BoneControllers/AnimNode_StrideWarping.h"
#include "Movement/Project_JMoverComponent.h"
#include "EnumColumn.h"
#include "ChooserIndexArray.h"
#include "MultiEnumColumn.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"

namespace
{
constexpr auto PlayerLocomotionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
UAnimBlueprint* Foundation()
{
 return LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionProductionGraph,"ProjectJ.PlayerLocomotion.ProductionGraph",PlayerLocomotionTestFlags)
bool FProjectJPlayerLocomotionProductionGraph::RunTest(const FString&)
{
 auto* BP=Foundation(); if (!TestNotNull(TEXT("GASP foundation"),BP)) return false;
 TestTrue(TEXT("Native GASP backend owns the production class"),BP->GeneratedClass && BP->GeneratedClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()));
 TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
 UAnimGraphNode_Slot* FullSlot=nullptr; UAnimGraphNode_Slot* UpperSlot=nullptr;
 UAnimGraphNode_LinkedAnimLayer* Procedural=nullptr; UAnimGraphNode_SaveCachedPose* Ground=nullptr;
 int32 FullSlots=0,UpperSlots=0,Stacks=0;
 TMap<FName,int32> ModernNodes;
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
 {
  const FName Type=Node->GetClass()->GetFName();
  TestFalse(TEXT("Retired custom locomotion/facing players cannot re-enter production"),Type==TEXT("AnimGraphNode_ProjectJLocomotion") || Type==TEXT("AnimGraphNode_ProjectJOneShotHandoff") || Type==TEXT("AnimGraphNode_ProjectJStrafeLowerBodyFacing") || Type==TEXT("AnimGraphNode_ProjectJStrafeUpperBodyFacing"));
  if (auto* Slot=Cast<UAnimGraphNode_Slot>(Node))
  {
   if (Slot->Node.SlotName==TEXT("DefaultSlot")) {FullSlot=Slot;++FullSlots;}
   if (Slot->Node.SlotName==TEXT("UpperBody")) {UpperSlot=Slot;++UpperSlots;}
  }
  if (auto* Layer=Cast<UAnimGraphNode_LinkedAnimLayer>(Node);Layer && Layer->Node.Layer==TEXT("Procedural_FullBody")) Procedural=Layer;
  if (auto* Cache=Cast<UAnimGraphNode_SaveCachedPose>(Node);Cache && Cache->CacheName==TEXT("ProjectJ_FinalGroundPose")) Ground=Cache;
  if (Cast<UAnimGraphNode_BlendStack>(Node)) ++Stacks;
  if(Type==TEXT("AnimGraphNode_Steering") || Type==TEXT("AnimGraphNode_OrientationWarping") ||
     Type==TEXT("AnimGraphNode_StrideWarping") || Type==TEXT("AnimGraphNode_OffsetRootBone") ||
     Type==TEXT("AnimGraphNode_DeadBlending") || Type==TEXT("AnimGraphNode_Inertialization") ||
     Type==TEXT("AnimGraphNode_ControlRig"))
  {
   ++ModernNodes.FindOrAdd(Type);
   bool Input=false,Output=false;
   for(const UEdGraphPin* Pin:Node->Pins)
    if(Pin->LinkedTo.Num()>0 && (Pin->PinType.PinSubCategoryObject==FPoseLink::StaticStruct() || Pin->PinType.PinSubCategoryObject==FComponentSpacePoseLink::StaticStruct()))
     {Input|=Pin->Direction==EGPD_Input;Output|=Pin->Direction==EGPD_Output;}
   TestTrue(TEXT("GASP correction/blending nodes retain connected pose inputs and outputs"),Input && Output);
  }
 }
 TestEqual(TEXT("One full-body montage owner"),FullSlots,1);TestEqual(TEXT("One separately masked equip owner"),UpperSlots,1);
 TestTrue(TEXT("Actual GASP playback stacks remain"),Stacks>0);
 for(const auto& Pair:TArray<TPair<FName,int32>>{{TEXT("AnimGraphNode_Steering"),2},{TEXT("AnimGraphNode_OrientationWarping"),1},{TEXT("AnimGraphNode_StrideWarping"),1},{TEXT("AnimGraphNode_OffsetRootBone"),1},{TEXT("AnimGraphNode_DeadBlending"),2},{TEXT("AnimGraphNode_Inertialization"),1},{TEXT("AnimGraphNode_ControlRig"),1}})
  TestEqual(TEXT("The full reviewed GASP locomotion correction chain remains authored"),ModernNodes.FindRef(Pair.Key),Pair.Value);
 // This source bank is retained for authoring/comparison. Its otherwise-unused
 // categories must still deserialize without collapsing map keys to an invalid tag.
 for(const TCHAR* Tag:{TEXT("Foley.Event.Slide.Loop"),TEXT("Foley.Event.Ragdoll"),TEXT("Foley.Event.Punch"),TEXT("Foley.Event.Kick")})
  TestTrue(TEXT("Retained GASP source-bank tags are registered"),FGameplayTag::RequestGameplayTag(Tag,false).IsValid());
 TestNotNull(TEXT("The retained source Foley bank loads with its tag dictionary"),LoadObject<UObject>(nullptr,TEXT("/Game/GASP58Native/Audio/Foley/DefaultFoleyEventAudioBank.DefaultFoleyEventAudioBank")));
 if (!FullSlot || !UpperSlot || !Procedural || !Ground) {AddError(TEXT("Missing on-foot pose contract"));return false;}
 TestTrue(TEXT("GASP source clocks keep updating underneath full-weight skills"),FullSlot->Node.bAlwaysUpdateSourcePose);
 const auto Linked=[](UEdGraphPin* A,UEdGraphPin* B){return A && B && A->LinkedTo.Num()==1 && A->LinkedTo[0]==B;};
 TestTrue(TEXT("Full-body skills override the result of all procedural feet/body corrections"),Linked(FullSlot->FindPin(TEXT("Source")),Procedural->FindPin(TEXT("Pose"))));
 TestTrue(TEXT("Mounted blending receives the final skill pose"),Linked(Ground->FindPin(TEXT("Pose")),FullSlot->FindPin(TEXT("Pose"))));
 TestTrue(TEXT("UpperBody stays in its masked layering branch"),UpperSlot->FindPin(TEXT("Pose")) && UpperSlot->FindPin(TEXT("Pose"))->LinkedTo.Num()==1 && UpperSlot->FindPin(TEXT("Pose"))->LinkedTo[0]->GetOwningNode()->GetClass()->GetFName()==TEXT("AnimGraphNode_LayeredBoneBlend"));
 for (const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"),TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C")})
 {
  const auto* Class=LoadClass<AProject_JPlayerCharacter>(nullptr,Path);if(!TestNotNull(TEXT("Production avatar"),Class))continue;
  const auto* Pawn=CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
  TestEqual(TEXT("Avatar uses the audited foundation"),Pawn->GetMesh()->GetAnimClass(),BP->GeneratedClass.Get());
  const auto* Profile=Pawn->GetLocomotionProfile();if(!TestNotNull(TEXT("Shared jump/movement policy remains"),Profile))continue;
  TestTrue(TEXT("Live jump reselection stays enabled"),Profile->AirPolicy.bEnableAirJumpReselection);
  TestNull(TEXT("Retired PSD route cannot be authored on the player"),FindFProperty<FProperty>(Pawn->GetClass(),TEXT("MotionMatchingAssetSet")));
  TestNull(TEXT("Retired PSD/search policy is absent from the profile schema"),FindFProperty<FProperty>(Profile->GetClass(),TEXT("MotionMatchingSearchPolicy")));
  TestNull(TEXT("Retired property-driven chooser state is absent"),FindFProperty<FProperty>(BP->GeneratedClass,TEXT("StateControllerPresentationStateForChooser")));
  TestNull(TEXT("GASP has one fixed state-controller decision path"),FindFProperty<FProperty>(BP->GeneratedClass,TEXT("LocomotionSetup")));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionGroundCandidates,"ProjectJ.PlayerLocomotion.GroundCandidateIntegrity",PlayerLocomotionTestFlags)
bool FProjectJPlayerLocomotionGroundCandidates::RunTest(const FString&)
{
 auto* BP=Foundation();if(!BP || !BP->GeneratedClass)return false;
 const auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject());
 auto* Chooser=Defaults->GroundAnimationChooser.Get();if(!TestNotNull(TEXT("Actual ground chooser"),Chooser))return false;
 const auto* OutputContext=Chooser->ContextData.IsValidIndex(1)?Chooser->ContextData[1].GetPtr<FContextObjectTypeStruct>():nullptr;
 TestTrue(TEXT("Native chooser output contract"),OutputContext && OutputContext->Struct==FProject_JPlayerGroundChooserOutput::StaticStruct());
 TArray<UObject*> Owned;GetObjectsWithOuter(Chooser,Owned,EGetObjectsFlags::IncludeNestedObjects);
 int32 Databases=0,EnabledEntries=0;
 for(auto* Object:Owned) if(auto* DB=Cast<UPoseSearchDatabase>(Object))
 {
  ++Databases;TestNotNull(TEXT("Embedded database retains its schema"),DB->Schema.Get());
  for(int32 Index=0;Index<DB->GetNumAnimationAssets();++Index)
  {
   const auto* Entry=DB->GetDatabaseAnimationAsset(Index);if(!Entry || !Entry->IsEnabled())continue;
   ++EnabledEntries;TestNotNull(TEXT("Enabled candidate retains its animation"),Entry->GetAnimationAsset());
   if(const auto* BlendSpace=Cast<UBlendSpace>(Entry->GetAnimationAsset()))
    TestTrue(TEXT("Pose matching retains its normalized phase sampling contract"),BlendSpace->bShouldMatchSyncPhases);
   TestTrue(TEXT("Authored candidate range stays ordered"),Entry->GetSamplingRange().Max>=Entry->GetSamplingRange().Min);
  }
  if(DB->GetNumAnimationAssets()==0)continue;
  using namespace UE::PoseSearch;
  TestTrue(TEXT("Production embedded index builds with its own schema and statistics"),FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(DB,ERequestAsyncBuildFlag::ContinueRequest|ERequestAsyncBuildFlag::WaitForCompletion)==EAsyncBuildIndexResult::Success);
  TestTrue(TEXT("Nonempty candidate database has searchable poses"),DB->GetSearchIndex().GetNumPoses()>0);
 }
 TestTrue(TEXT("GASP owns embedded candidate databases"),Databases>=5);TestTrue(TEXT("Actual GASP candidate inventory is populated"),EnabledEntries>0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGroundLoopRootMotion,"ProjectJ.PlayerLocomotion.GroundLoopRootMotion",PlayerLocomotionTestFlags)
bool FProjectJGroundLoopRootMotion::RunTest(const FString&)
{
 // Exercise the engine's actual sample tick, not just the author's checkbox.
 // Steering needs nonzero root deltas even when the capsule is already facing
 // its target. Repeat across loop wraps and every authored slope sample.
 // Current production styles plus the retained Relaxed authoring variant.
 for (const TCHAR* RelativePath : {
  TEXT("Walk/BS_Relaxed_Walk_Loop_F_Slopes"),
  TEXT("Run/BS_Neutral_Run_Loop_F"),
  TEXT("Run/BS_Relaxed_Run_Loop_F_Slopes")})
 {
  const FString Path=FString(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/"))+RelativePath;
  auto* BlendSpace=LoadObject<UBlendSpace>(nullptr,*Path);
  if (!TestNotNull(TEXT("Production ground loop"),BlendSpace)) continue;
  for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
  {
   TArray<FBlendSampleData> Cache; FBlendFilter Filter; BlendSpace->InitializeFilter(&Filter);
   FMarkerTickRecord Markers; FDeltaTimeRecord Delta; float Time=.125f;
   int32 MissingDeltas=0; double Distance=0.;
   for (int32 Frame=0; Frame<300; ++Frame)
   {
    FAnimTickRecord Record(BlendSpace,Sample.SampleValue,Cache,Filter,true,1.f,false,false,1.f,Time,Markers);
    Record.DeltaTimeRecord=&Delta;
    const TArray<FName> EmptyMarkers;
    const auto* ValidMarkers=BlendSpace->GetUniqueMarkerNames();
    FAnimAssetTickContext Context(1.f/60.f,ERootMotionMode::RootMotionFromEverything,true,ValidMarkers?*ValidMarkers:EmptyMarkers);
    FAnimNotifyQueue Notifies;
    BlendSpace->TickAssetPlayer(Record,Notifies,Context);
    const double Step=Context.RootMotionMovementParams.GetRootMotionTransform().GetTranslation().Size();
    if (Frame>=2) { MissingDeltas+=!FMath::IsFinite(Step) || Step<=UE_KINDA_SMALL_NUMBER; Distance+=Step; }
   }
   TestEqual(FString::Printf(TEXT("%s sample %.1f retains every root delta through loop wraps"),RelativePath,Sample.SampleValue.X),MissingDeltas,0);
   TestTrue(TEXT("Loop provides animated translation for steering and stride warping"),Distance>100.);
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGroundSpeedAndCadence,"ProjectJ.PlayerLocomotion.GroundSpeedAndCadence",PlayerLocomotionTestFlags)
bool FProjectJGroundSpeedAndCadence::RunTest(const FString&)
{
 for(const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player_Master.BP_Player_Master_C"),TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C")})
 {
  const auto* Class=LoadClass<AProject_JPlayerCharacter>(nullptr,Path);if(!TestNotNull(TEXT("Production player"),Class))continue;
  const auto* Pawn=Class->GetDefaultObject<AProject_JPlayerCharacter>();
  TestEqual(TEXT("Run admission matches tuned simulation speed"),Pawn->GetMoverMaxSpeed(false),500.f);
  TestEqual(TEXT("Sprint admission matches tuned simulation speed"),Pawn->GetMoverMaxSpeed(true),750.f);
  const auto* Mover=CastChecked<UProject_JMoverComponent>(Pawn->GetMover());
  TestEqual(TEXT("Run simulation and prediction target"),Mover->GroundMovementSettings.RunSpeed,500.f);
  TestEqual(TEXT("Sprint simulation and prediction target"),Mover->GroundMovementSettings.SprintSpeed,750.f);
  TestEqual(TEXT("Ctrl Walk is preserved"),Mover->GroundMovementSettings.WalkSpeed,165.f);
 }
 const auto* BP=Foundation();if(!BP || !BP->GeneratedClass)return false;
 int32 Strides=0;
 for(TFieldIterator<FStructProperty> Property(BP->GeneratedClass);Property;++Property)
  if(Property->Struct==FAnimNode_StrideWarping::StaticStruct())
  {
   ++Strides;
   const auto* Stride=Property->ContainerPtrToValuePtr<FAnimNode_StrideWarping>(BP->GeneratedClass->GetDefaultObject());
   TestTrue(TEXT("Stride expansion is bounded and interpolated"),Stride->StrideScaleModifier.bClampResult && Stride->StrideScaleModifier.bInterpResult);
   TestEqual(TEXT("Compiled stride can accommodate increased speed without time scaling"),Stride->StrideScaleModifier.ClampMax,1.5f);
   TestTrue(TEXT("FK leg reach and thigh compensation remain enabled"),Stride->bClampIKUsingFKLimits && Stride->bCompensateIKUsingFKThighRotation);
  }
 // BlendStack compiles its authored sample graph for multiple state players.
 // Every runtime instance above must retain the limits, not just one template.
 TestTrue(TEXT("Compiled stride instances were checked"),Strides>0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNeutralLocomotionSelection,"ProjectJ.PlayerLocomotion.NeutralSelection",PlayerLocomotionTestFlags)
bool FProjectJNeutralLocomotionSelection::RunTest(const FString&)
{
 const auto* BP=Foundation();if(!BP || !BP->GeneratedClass)return false;
 const auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject());
 TSet<const UObject*> Seen;int32 NeutralClips=0,WalkClips=0;
 TFunction<void(const UObject*)> Inspect;
 Inspect=[&](const UObject* Object)
 {
  if(!Object || Seen.Contains(Object))return;Seen.Add(Object);
  if(const auto* Asset=Cast<UAnimationAsset>(Object))
  {
   const FString Path=Asset->GetPathName();
   if(Path.Contains(TEXT("/Run/")) || Path.Contains(TEXT("/Sprint/")) || Path.Contains(TEXT("/Jump/")))
   {
    ++NeutralClips;
    TestTrue(TEXT("Selected Run/Sprint/Jump assets use the Neutral family: ")+Path,
     Asset->GetName().StartsWith(TEXT("M_Neutral_")) || Asset->GetName().StartsWith(TEXT("BS_Neutral_")));
   }
   if(Path.Contains(TEXT("/Walk/M_Relaxed_Walk_Loop")))++WalkClips;
   if(const auto* Space=Cast<UBlendSpace>(Asset))for(const auto& Sample:Space->GetBlendSamples())Inspect(Sample.Animation);
  }
  else if(const auto* Database=Cast<UPoseSearchDatabase>(Object))
  {
   for(int32 Index=0;Index<Database->GetNumAnimationAssets();++Index)
    if(const auto* Entry=Database->GetDatabaseAnimationAsset(Index);Entry && Entry->IsEnabled())Inspect(Entry->GetAnimationAsset());
  }
  else if(const auto* Table=Cast<UChooserTable>(Object))
  {
   auto Follow=[&](const FInstancedStruct& Result)
   {
    if(Result.GetScriptStruct()==FAssetChooser::StaticStruct())Inspect(Result.Get<FAssetChooser>().Asset);
    else if(Result.GetScriptStruct()==FEvaluateChooser::StaticStruct())Inspect(Result.Get<FEvaluateChooser>().Chooser);
    else if(Result.GetScriptStruct()==FNestedChooser::StaticStruct())Inspect(Result.Get<FNestedChooser>().Chooser);
   };
   for(int32 Index=0;Index<Table->ResultsStructs.Num();++Index)
    if(!Table->DisabledRows.IsValidIndex(Index) || !Table->DisabledRows[Index])Follow(Table->ResultsStructs[Index]);
   Follow(Table->FallbackResult);
  }
 };
 Inspect(Defaults->GroundAnimationChooser.Get());Inspect(Defaults->AirAnimationChooser.Get());
 TestTrue(TEXT("Actual ground/air selection and embedded databases were followed"),NeutralClips>100);
 TestTrue(TEXT("Existing Relaxed Walk is preserved"),WalkClips>0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionAirNotifies,"ProjectJ.PlayerLocomotion.AirAnimationNotifies",PlayerLocomotionTestFlags)
bool FProjectJPlayerLocomotionAirNotifies::RunTest(const FString&)
{
 auto* BP=Foundation();if(!BP || !BP->GeneratedClass)return false;
 const auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject());
 const auto* Root=Defaults->AirAnimationChooser.Get();if(!TestNotNull(TEXT("Production air chooser"),Root))return false;
 TArray<const UChooserTable*> Pending{Root};TSet<const UChooserTable*> Seen;TSet<const UAnimSequence*> Sequences;
 while(!Pending.IsEmpty())
 {
  const auto* Table=Pending.Pop(EAllowShrinking::No);if(!Table || Seen.Contains(Table))continue;Seen.Add(Table);
  auto Follow=[&](const FInstancedStruct& Result)
  {
   if(Result.GetScriptStruct()==FAssetChooser::StaticStruct())
   {
    UObject* Asset=Result.Get<FAssetChooser>().Asset;
    if(const auto* Sequence=Cast<UAnimSequence>(Asset))Sequences.Add(Sequence);
    else if(const auto* Child=Cast<UChooserTable>(Asset))Pending.Add(Child);
   }
   else if(Result.GetScriptStruct()==FEvaluateChooser::StaticStruct())Pending.Add(Result.Get<FEvaluateChooser>().Chooser);
   else if(Result.GetScriptStruct()==FNestedChooser::StaticStruct())Pending.Add(Result.Get<FNestedChooser>().Chooser);
  };
  for(int32 Index=0;Index<Table->ResultsStructs.Num();++Index)
   if(!Table->DisabledRows.IsValidIndex(Index) || !Table->DisabledRows[Index])Follow(Table->ResultsStructs[Index]);
  Follow(Table->FallbackResult);
 }
 TestTrue(TEXT("The actual air chooser still supplies jump and fall clips"),Sequences.Num()>1);
 for(const auto* Sequence:Sequences)for(const auto& Event:Sequence->Notifies)
 {
  TestNotEqual(TEXT("Retired jump callbacks cannot return through authored clips"),Event.NotifyName,FName(TEXT("Project_JLocomotionAnimNotify")));
  if(!Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent")))continue;
  TestNotNull(TEXT("Air Foley has an executable native payload"),Cast<UProject_JAnimNotify_FoleyEvent>(Event.Notify));
  TestFalse(TEXT("Air cosmetic contacts never execute on dedicated servers"),Event.bTriggerOnDedicatedServer);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionNativeCallbacks,"ProjectJ.PlayerLocomotion.NativeCallbackOwnership",PlayerLocomotionTestFlags)
bool FProjectJPlayerLocomotionNativeCallbacks::RunTest(const FString&)
{
 auto* BP=Foundation();if(!TestNotNull(TEXT("Foundation"),BP))return false;
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
 for(FName Name:{FName(TEXT("IsMoving")),FName(TEXT("IsStarting")),FName(TEXT("ShouldSpinTransition"))})
  TestFalse(TEXT("Pure queries no longer take a BP forwarding frame"),Graphs.ContainsByPredicate([&](auto* G){return G->GetFName()==Name;}));
 for(const auto& Pair:TArray<TPair<FName,FName>>{{TEXT("OnStateEntry_InAirTransition"),TEXT("EnterNativeAirTransition")},{TEXT("OnStateEntry_InAirLoop"),TEXT("EnterNativeAirLoop")}})
 {
  auto** Found=Graphs.FindByPredicate([&](auto* Graph){return Graph->GetFName()==Pair.Key;});
  if(!TestNotNull(TEXT("GASP callback graph"),Found?*Found:nullptr))continue;
  int32 Calls=0;for(UEdGraphNode* Node:(*Found)->Nodes) if(auto* Call=Cast<UK2Node_CallFunction>(Node)){++Calls;TestEqual(TEXT("Callback delegates to the native owner"),Call->FunctionReference.GetMemberName(),Pair.Value);}
  TestEqual(TEXT("Exactly one calculation owner per callback"),Calls,1);
 }
 for (UEdGraph* Graph:BP->UbergraphPages) TestTrue(TEXT("No duplicate event lifecycle writers"),Graph->Nodes.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDirectNativeQueries,"ProjectJ.PlayerLocomotion.DirectNativeQueriesAndWrist",PlayerLocomotionTestFlags)
bool FProjectJDirectNativeQueries::RunTest(const FString&)
{
 auto* BP=Foundation();if(!BP)return false;TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
 int32 Warping=0;
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)if(auto* Call=Cast<UK2Node_CallFunction>(Node))
 {
  TestNotEqual(TEXT("No stride query wrapper remains"),Call->GetFunctionName(),FName(TEXT("Get_StrideWarpAlpha")));
  TestNotEqual(TEXT("No strafe query wrapper remains"),Call->GetFunctionName(),FName(TEXT("Get_StrafeWarpAlpha")));
  if(Call->GetFunctionName()==GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateWarpAlpha))
  {++Warping;TestTrue(TEXT("Each warping consumer retains its player reference"),Call->FindPinChecked(TEXT("Node"))->LinkedTo.Num()==1);}
 }
 TestEqual(TEXT("Distinct stride and orientation consumers remain"),Warping,2);
 auto* Body=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.ABP_Greatsword_Woman_RunTIme"));if(!TestNotNull(TEXT("Production retarget body"),Body))return false;
 Graphs.Reset();Body->GetAllGraphs(Graphs);int32 Wrist=0;
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
 {
  if(auto* Call=Cast<UK2Node_CallFunction>(Node))TestNotEqual(TEXT("Already computed wrist target is not recomposed in BP"),Call->GetFunctionName(),FName(TEXT("MakeTransform")));
  if(auto* Var=Cast<UK2Node_VariableGet>(Node);Var && Var->VariableReference.GetMemberName()==GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightWristTarget))++Wrist;
 }
 TestEqual(TEXT("The primary hand directly consumes its native target"),Wrist,1);
 auto* Layer=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers.ABP_Greatsword_Layers"));if(!TestNotNull(TEXT("Weapon layer"),Layer))return false;
 for(UEdGraph* Event:Layer->UbergraphPages)TestTrue(TEXT("Weapon layer has no disconnected event leftovers"),Event->Nodes.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionMontageAuthoring,"ProjectJ.PlayerLocomotion.MontageAuthoringIdempotence",PlayerLocomotionTestFlags)
bool FProjectJPlayerLocomotionMontageAuthoring::RunTest(const FString&)
{
 auto* BP=Foundation();if(!BP)return false;
 TStrongObjectPtr<UAnimBlueprint> Copy(DuplicateObject<UAnimBlueprint>(BP,GetTransientPackage()));
 const bool WasDirty=BP->GetOutermost()->IsDirty();TArray<UEdGraph*> Graphs;Copy->GetAllGraphs(Graphs);
 int32 Before=0;for(auto* Graph:Graphs)Before+=Graph->Nodes.Num();
 TestTrue(TEXT("Production template already has the corrected order"),UProject_JPlayerLocomotionLibrary::RepairFullBodyMontageOrder(Copy.Get()));
 TestTrue(TEXT("Reapplying creates no second montage owner"),UProject_JPlayerLocomotionLibrary::RepairFullBodyMontageOrder(Copy.Get()));
 int32 After=0;for(auto* Graph:Graphs)After+=Graph->Nodes.Num();TestEqual(TEXT("No extra graph nodes"),After,Before);
 TestEqual(TEXT("Authoring test does not dirty production"),BP->GetOutermost()->IsDirty(),WasDirty);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerNativeStateContract,"ProjectJ.PlayerLocomotion.NativeStateAndPinContract",PlayerLocomotionTestFlags)
bool FProjectJPlayerNativeStateContract::RunTest(const FString&)
{
 auto* BP=Foundation();if(!TestNotNull(TEXT("Production master"),BP))return false;
 TestEqual(TEXT("Animation state has one native owner, with no duplicate BP variables"),BP->NewVariables.Num(),0);
 const auto* Native=UProject_JPlayerAnimInstance::StaticClass();
 const auto* State=FindFProperty<FEnumProperty>(BP->GeneratedClass,GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerStateMachineState));
 TestTrue(TEXT("Playback state uses the inherited native enum"),State && State->GetOwnerClass()==Native && State->GetEnum()==StaticEnum<EProject_JPlayerPlaybackState>());
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);int32 Rules=0,PlayerCurves=0;
 for(auto* Graph:Graphs)
 {
  int32 RuleCalls=0;
  for(UEdGraphNode* Node:Graph->Nodes)
  {
   TestNull(TEXT("No BP variable setter can duplicate the native state writer"),Cast<UK2Node_VariableSet>(Node));
   if(auto* Call=Cast<UK2Node_CallFunction>(Node))
   {
    if(Call->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateNativeTransition))
    {
     ++Rules;++RuleCalls;const auto* Clock=Call->FindPin(TEXT("StateElapsedTime"));
     if(Clock && !Clock->LinkedTo.IsEmpty())TestNotNull(TEXT("Elapsed time is read from this transition's engine state getter"),Cast<UK2Node_AnimGetter>(Clock->LinkedTo[0]->GetOwningNode()));
    }
    if(Call->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTurnInPlaceSteeringAlpha))
    {++PlayerCurves;TestTrue(TEXT("Per-player curve query keeps its node reference"),Call->FindPinChecked(TEXT("Node"))->LinkedTo.Num()==1);}
   }
  }
  if(RuleCalls)TestEqual(TEXT("One native predicate owns each transition"),RuleCalls,1);
 }
 TestEqual(TEXT("All authored locomotion transitions use native predicates"),Rules,21);
 TestEqual(TEXT("Steering curve is evaluated per BlendStack player"),PlayerCurves,1);
 FCompilerResultsLog Compiler;Compiler.bSilentMode=true;FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::SkipGarbageCollection,&Compiler);
 TestEqual(TEXT("Master compiles without errors"),Compiler.NumErrors,0);TestEqual(TEXT("Master compiles without warnings"),Compiler.NumWarnings,0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStandingStopFamily,"ProjectJ.PlayerLocomotion.StandingStopFamily",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJStandingStopFamily::RunTest(const FString&)
{
 auto* Root=LoadObject<UChooserTable>(nullptr,TEXT("/Game/Animation_Logic/Chooser/CHT_ProjectJ_Ground.CHT_ProjectJ_Ground"));
 if (!TestNotNull(TEXT("Production ground chooser"),Root)) return false;
 TArray<UObject*> Nested;GetObjectsWithOuter(Root,Nested,EGetObjectsFlags::IncludeNestedObjects);
 UChooserTable* Stops=nullptr;for (UObject* Object:Nested) if (auto* T=Cast<UChooserTable>(Object);T && T->GetFName()==TEXT("Stand Stops")) Stops=T;
 if (!TestNotNull(TEXT("Standing stop candidates"),Stops)) return false;
 auto* Mesh=NewObject<USkeletalMeshComponent>();auto* Anim=NewObject<UProject_JPlayerAnimInstance>(Mesh);
 using Gait=EProject_JLocomotionGaitIntent;
 struct FCase {Gait Recent;Gait Current;float Speed;const TCHAR* Family;};
 const FCase Cases[]={{Gait::Walk,Gait::Walk,165.f,TEXT("Walk")},{Gait::Run,Gait::Run,500.f,TEXT("Run")},
  {Gait::Sprint,Gait::Sprint,750.f,TEXT("Sprint")},{Gait::Sprint,Gait::Run,745.f,TEXT("Sprint")},
  {Gait::Run,Gait::Walk,490.f,TEXT("Run")},{Gait::Run,Gait::Run,100.f,TEXT("Walk")}};
 for (const auto& Case:Cases)
 {
  Anim->PlayerGaitRecent=Case.Recent;Anim->PlayerGait=Case.Current;Anim->PlayerSpeed2D=Case.Speed;
  Anim->PlayerMovementDirectionRecent=EProject_JPlayerMovementDirection::Forward;Anim->PlayerFutureFacingDelta=0.;
  FChooserEvaluationContext Context;Context.AddObjectParam(Anim);FProject_JPlayerGroundChooserOutput Output;Context.AddStructParam(Output);
  TArray<FChooserIndexArray::FIndexData> A,B;A.Reserve(Stops->ResultsStructs.Num());B.SetNumUninitialized(Stops->ResultsStructs.Num());
  for (int32 Index=0;Index<Stops->ResultsStructs.Num();++Index) if (!Stops->IsRowDisabled(Index)) A.Emplace(Index,0.f);
  FChooserIndexArray In(A.GetData(),Stops->ResultsStructs.Num()),Out(B.GetData(),Stops->ResultsStructs.Num());In.SetNum(A.Num());
  for (const auto& Struct:Stops->ColumnsStructs) if (const auto* Column=Struct.GetPtr<FChooserColumnBase>();Column && Column->HasFilters() && !Column->HasCosts() && !Column->bDisabled)
  {Out.SetNum(0);Column->Filter(Context,In,Out);In=Out;}
  TestFalse(TEXT("Every supported stopping condition retains candidates"),In.IsEmpty());
  for (const auto& Index:In)
  {
   const auto* Choice=Stops->ResultsStructs[Index.Index].GetPtr<FAssetChooser>();
   TestTrue(TEXT("Pose search cannot choose a different stop gait"),Choice && Choice->Asset && Choice->Asset->GetPathName().Contains(FString::Printf(TEXT("/Locomotion/%s/"),Case.Family)));
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGroundedLandingPhase,"ProjectJ.PlayerLocomotion.GroundedLandingPhase",PlayerLocomotionTestFlags)
bool FProjectJGroundedLandingPhase::RunTest(const FString&)
{
 auto* BP=Foundation();if (!BP || !BP->GeneratedClass) return false;
 auto* Root=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject())->GroundAnimationChooser.Get();
 TArray<UObject*> Owned;GetObjectsWithOuter(Root,Owned,EGetObjectsFlags::IncludeNestedObjects);
 TSet<UPoseSearchDatabase*> Built;int32 LandingRows=0;
 const auto* RowsProperty=FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(),TEXT("RowValues"));
 if (!TestNotNull(TEXT("Serialized pose-search rows"),RowsProperty)) return false;
 for (UObject* Object:Owned) if (auto* Table=Cast<UChooserTable>(Object))
 {
  const FMultiEnumColumn* Modes=nullptr;
  for (const auto& C:Table->ColumnsStructs) if (const auto* M=C.GetPtr<FMultiEnumColumn>())
   if (const auto* P=M->InputValue.GetPtr<FEnumContextProperty>();P &&
    P->Binding.PropertyBindingChain==TArray<FName>{GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerMovementModeRecent)}) Modes=M;
  if (!Modes) continue;
  for (auto& C:Table->ColumnsStructs) if (auto* Search=C.GetMutablePtr<FPoseSearchColumn>())
   for (int32 Row=0;Row<Search->GetNumRows();++Row)
   {
    if ((Table->DisabledRows.IsValidIndex(Row) && Table->DisabledRows[Row]) || !Modes->RowValues.IsValidIndex(Row) || Modes->RowValues[Row].Value!=2u) continue;
    const auto& Value=(*RowsProperty->ContainerPtrToValuePtr<TArray<FPoseSearchColumnRow>>(Search))[Row];
    const auto* Sequence=Cast<UAnimSequence>(Value.Data.GetAnimationAsset());
    if (!TestNotNull(TEXT("Grounded landing sequence"),Sequence)) continue;
    float Impact=MAX_flt;
    for (const FAnimNotifyEvent& Event:Sequence->Notifies)
     if (const auto* Foley=Cast<UProject_JAnimNotify_FoleyEvent>(Event.Notify);Foley && Foley->FoleyEvent.Event==Project_J::Foley::Land) Impact=FMath::Min(Impact,Event.GetTime());
    TestTrue(TEXT("Landing retains its authored impact event"),Impact<Sequence->GetPlayLength());
    auto* DB=Value.Database.Get();if (!TestNotNull(TEXT("Landing's actual embedded database"),DB)) continue;
    using namespace UE::PoseSearch;
    if (!Built.Contains(DB))
    {
     if (!TestTrue(TEXT("Landing index builds"),FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(DB,ERequestAsyncBuildFlag::ContinueRequest|ERequestAsyncBuildFlag::WaitForCompletion)==EAsyncBuildIndexResult::Success)) return false;
     Built.Add(DB);
    }
    int32 Source=INDEX_NONE;
    for (int32 I=0;I<DB->GetNumAnimationAssets();++I) if (DB->GetDatabaseAnimationAsset(I)==&Value.Data) {Source=I;break;}
    TestTrue(TEXT("Chooser row maps to its database entry"),Source!=INDEX_NONE);
    const auto& Index=DB->GetSearchIndex();int32 Selectable=0;
    for (const auto& Asset:Index.Assets) if (Asset.GetSourceAssetIdx()==Source)
     for (int32 Pose=Asset.GetFirstPoseIdx();Pose<Asset.GetFirstPoseIdx()+Asset.GetNumPoses();++Pose)
      if (!Index.PoseMetadata[Pose].IsBlockTransition())
      {
       ++Selectable;
       const float Time=Asset.GetTimeFromPoseIndex(Pose,DB->Schema->SampleRate);
       TestTrue(TEXT("Physical landing cannot replay the airborne approach"),Time>=Impact-.1f);
      }
    TestTrue(TEXT("Every landing keeps a searchable impact pose"),Selectable>0);++LandingRows;
   }
 }
 TestTrue(TEXT("Standing, walking, running and sprint landing rows were inspected"),LandingRows>=32);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTransitionSearchAvailability,"ProjectJ.PlayerLocomotion.TransitionSearchAvailability",PlayerLocomotionTestFlags)
bool FProjectJTransitionSearchAvailability::RunTest(const FString&)
{
 auto* BP=Foundation();if (!BP || !BP->GeneratedClass) return false;
 auto* Root=CastChecked<UProject_JPlayerAnimInstance>(BP->GeneratedClass->GetDefaultObject())->GroundAnimationChooser.Get();
 const auto* RowsProperty=FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(),TEXT("RowValues"));
 if (!TestNotNull(TEXT("Serialized search-row contract"),RowsProperty)) return false;
 TArray<UChooserTable*> Pending{Root};TSet<UChooserTable*> Seen;TSet<UPoseSearchDatabase*> Built;
 int32 RowsChecked=0;
 while (!Pending.IsEmpty())
 {
  auto* Table=Pending.Pop(EAllowShrinking::No);if (!Table || Seen.Contains(Table)) continue;Seen.Add(Table);
  auto Follow=[&](const FInstancedStruct& Result)
  {
   if (const auto* Nested=Result.GetPtr<FNestedChooser>()) Pending.Add(Nested->Chooser);
   else if (const auto* Evaluate=Result.GetPtr<FEvaluateChooser>()) Pending.Add(Evaluate->Chooser);
   else if (const auto* Asset=Result.GetPtr<FAssetChooser>()) if (auto* Child=Cast<UChooserTable>(Asset->Asset)) Pending.Add(Child);
  };
  for (int32 Row=0;Row<Table->ResultsStructs.Num();++Row)
   if (!Table->DisabledRows.IsValidIndex(Row) || !Table->DisabledRows[Row]) Follow(Table->ResultsStructs[Row]);
  Follow(Table->FallbackResult);
  for (auto& C:Table->ColumnsStructs) if (auto* Search=C.GetMutablePtr<FPoseSearchColumn>())
  {
   const auto& Rows=*RowsProperty->ContainerPtrToValuePtr<TArray<FPoseSearchColumnRow>>(Search);
   for (int32 Row=0;Row<Rows.Num();++Row)
   {
    if (Table->DisabledRows.IsValidIndex(Row) && Table->DisabledRows[Row]) continue;
    const auto& Value=Rows[Row];if (!Value.Data.IsEnabled()) continue;
    auto* Animation=Cast<UAnimationAsset>(Value.Data.GetAnimationAsset());
    const FString Label=FString::Printf(TEXT("%s row %d (%s)"),*Table->GetName(),Row,*GetNameSafe(Animation));
    if (!TestNotNull(*Label,Animation)) continue;
    auto* DB=Value.Database.Get();if (!TestNotNull(*Label,DB)) continue;
    using namespace UE::PoseSearch;
    if (!Built.Contains(DB))
    {
     if (!TestTrue(*Label,FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(DB,ERequestAsyncBuildFlag::ContinueRequest|ERequestAsyncBuildFlag::WaitForCompletion)==EAsyncBuildIndexResult::Success)) return false;
     Built.Add(DB);
    }
    int32 Source=INDEX_NONE;
    for (int32 I=0;I<DB->GetNumAnimationAssets();++I) if (DB->GetDatabaseAnimationAsset(I)==&Value.Data) {Source=I;break;}
    TestTrue(*Label,Source!=INDEX_NONE);
    const auto& Index=DB->GetSearchIndex();int32 Selectable=0;
    const auto* Sequence=Cast<UAnimSequence>(Animation);
    for (const auto& Asset:Index.Assets) if (Asset.GetSourceAssetIdx()==Source)
     for (int32 Pose=Asset.GetFirstPoseIdx();Pose<Asset.GetFirstPoseIdx()+Asset.GetNumPoses();++Pose)
      if (!Index.PoseMetadata[Pose].IsBlockTransition())
      {
       ++Selectable;const float Time=Asset.GetTimeFromPoseIndex(Pose,DB->Schema->SampleRate);
       TestTrue(*Label,FMath::IsFinite(Time) && Time>=0.f);
       if (Sequence) TestTrue(*Label,Time<=Sequence->GetPlayLength()+UE_KINDA_SMALL_NUMBER);
       if (Sequence) for (const auto& Event:Sequence->Notifies)
        if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_PoseSearchExcludeFromDatabase>())
         TestFalse(TEXT("An excluded sequence segment cannot become an entry pose"),Time>Event.GetTime()+UE_KINDA_SMALL_NUMBER && Time<Event.GetEndTriggerTime()-UE_KINDA_SMALL_NUMBER);
      }
    TestTrue(*FString::Printf(TEXT("%s retains an entry pose outside transition blocks"),*Label),Selectable>0);
    ++RowsChecked;
   }
  }
 }
 AddInfo(FString::Printf(TEXT("Inspected %d active search rows in %d reachable tables"),RowsChecked,Seen.Num()));
 TestTrue(TEXT("Ground transition families were actually inspected"),RowsChecked>200);
 return true;
}
#endif
