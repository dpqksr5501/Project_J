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
 for (const TCHAR* Path:{TEXT("/Game/Character_BPs/BP_Player.BP_Player_C"),TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C")})
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
 for (const TCHAR* Gait : {TEXT("Walk"), TEXT("Run")})
 {
  const FString Path=FString::Printf(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/%s/BS_Relaxed_%s_Loop_F_Slopes"),Gait,Gait);
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
   TestEqual(FString::Printf(TEXT("%s slope %.1f retains every root delta through loop wraps"),Gait,Sample.SampleValue.X),MissingDeltas,0);
   TestTrue(TEXT("Loop provides animated translation for steering and stride warping"),Distance>100.);
  }
 }
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
#endif
