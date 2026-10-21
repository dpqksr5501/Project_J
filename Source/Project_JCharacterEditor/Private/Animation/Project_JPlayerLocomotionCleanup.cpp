#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_MotionMatching.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_Root.h"
#include "AnimStateNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Editor.h"
#include "Chooser.h"
#include "StructUtils/InstancedStruct.h"
#include "Engine/MemberReference.h"
#include "K2Node_VariableGet.h"
#include "UObject/UnrealType.h"
#include "KismetCompiler.h"
#include "Misc/DefaultValueHelper.h"
#include "AnimGraphNode_LinkedAnimLayer.h"

namespace
{
UEdGraph* FindGraph(UBlueprint* Blueprint,FName Name)
{
 TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
 for (auto* Graph:Graphs) if (Graph->GetFName()==Name) return Graph;
 return nullptr;
}

bool MakePosePassthrough(UAnimBlueprint* Target,UEdGraph* Graph)
{
 if (!Graph) return false;
 UAnimGraphNode_LinkedInputPose* Input=nullptr;UAnimGraphNode_Root* Output=nullptr;
 for (UEdGraphNode* Node:Graph->Nodes)
 {
  if (auto* Candidate=Cast<UAnimGraphNode_LinkedInputPose>(Node)) Input=Candidate;
  if (auto* Candidate=Cast<UAnimGraphNode_Root>(Node)) Output=Candidate;
 }
 if (!Input || !Output) return false;
 for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
 {
  Node->BreakAllNodeLinks();
  if (Node!=Input && Node!=Output) FBlueprintEditorUtils::RemoveNode(Target,Node,true);
 }
 return Graph->GetSchema()->TryCreateConnection(Input->FindPin(TEXT("Pose")),Output->FindPin(TEXT("Result")));
}
}

TArray<FString> UProject_JPlayerLocomotionLibrary::CleanNativeLocomotionBlueprint(UAnimBlueprint* Target)
{
 TArray<FString> Report;
 if (!Target || !Target->ParentClass || !Target->ParentClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass())) return {TEXT("ERROR: Not a native Project J foundation")};
 auto* Main=FindGraph(Target,TEXT("AnimGraph"));
 if (!Main) return {TEXT("ERROR: Missing AnimGraph")};
 Target->Modify();
 // Native source publication always selected index 1. Remove the alternate MM
 // player and selector, retaining the state-controller/blend-stack pair intact.
 for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Main->Nodes))
 {
  if (auto* Selector=Cast<UAnimGraphNode_BlendListByInt>(Node);Selector && Selector->GetFName()==TEXT("AnimGraphNode_BlendListByInt_0"))
  {
   auto* Input=Selector->FindPin(TEXT("BlendPose_1"));auto* Output=Selector->FindPin(TEXT("Pose"));
   if (!Input || Input->LinkedTo.Num()!=1 || !Output) return {TEXT("ERROR: Unexpected locomotion selector contract")};
   const auto Before=Input->LinkedTo,After=Output->LinkedTo;
   Selector->BreakAllNodeLinks();
   for (auto* A:Before) for (auto* B:After)
    if (!Main->GetSchema()->TryCreateConnection(A,B)) return {TEXT("ERROR: Cannot connect state controller")};
   FBlueprintEditorUtils::RemoveNode(Target,Selector,true);Report.Add(TEXT("Removed inactive MM/setup selector"));
  }
 }
 for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Main->Nodes))
  if (Cast<UAnimGraphNode_MotionMatching>(Node)) {FBlueprintEditorUtils::RemoveNode(Target,Node,true);Report.Add(TEXT("Removed alternate MM player and per-input graph"));}

 TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);
 TArray<UAnimStateNode*> States;
 for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
  if (auto* State=Cast<UAnimStateNode>(Node);State && State->BoundGraph &&
   (State->BoundGraph->GetFName()==TEXT("Slide Transition") || State->BoundGraph->GetFName()==TEXT("Slide Loop") || State->BoundGraph->GetFName()==TEXT("Debug Flying Loop"))) States.Add(State);
 for (auto* State:States)
 {
  Report.Add(TEXT("Removed excluded state and transitions: ")+State->BoundGraph->GetName());
  // The animation schema removes its bound graph and incident transition graphs.
  FBlueprintEditorUtils::RemoveNode(Target,State,true);
 }
 for (FName Name:{FName(TEXT("Procedural_PreLayering")),FName(TEXT("Procedural_FullBody"))})
 {
  if(Name==TEXT("Procedural_PreLayering") && Target->NewVariables.IsEmpty() && !FindGraph(Target,Name))continue;
  if (!MakePosePassthrough(Target,FindGraph(Target,Name))) return {TEXT("ERROR: Missing procedural layer contract")};
  Report.Add(TEXT("Removed excluded slide/MMI processing from ")+Name.ToString());
 }
 // The imported notify interface is shared with authored animation assets. Keep
 // its signature as a no-op for excluded MMI, rather than breaking those callers.
 if (auto* Graph=FindGraph(Target,TEXT("Update_MMIConstraints")))
 {
  UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;
  for (UEdGraphNode* Node:Graph->Nodes) {if (auto* E=Cast<UK2Node_FunctionEntry>(Node)) Entry=E;if (auto* R=Cast<UK2Node_FunctionResult>(Node)) Result=R;}
  if (!Entry) return {TEXT("ERROR: Missing interface entry")};
  for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
  {Node->BreakAllNodeLinks();if (Node!=Entry && Node!=Result) FBlueprintEditorUtils::RemoveNode(Target,Node,true);}
  Entry->LocalVariables.Reset();
  if (Result && !Graph->GetSchema()->TryCreateConnection(Entry->GetThenPin(),Result->FindPin(UEdGraphSchema_K2::PN_Execute))) return {TEXT("ERROR: Interface terminator")};
  Report.Add(TEXT("Retained MMI interface signature without excluded gameplay logic"));
 }

 // Only reviewed migration leftovers are candidates. Engine reference checks
 // include child BPs, delegates, anim callbacks and property-access bindings.
 const FName Functions[]={TEXT("InitializeMoverPredictor"),TEXT("Update_PropertiesFromCharacter"),TEXT("Update_Logic"),TEXT("Update_Trajectory"),TEXT("Update_EssentialValues"),TEXT("Update_States"),TEXT("Update_AimOffset"),TEXT("Update_AdditiveLean"),TEXT("DebugDraws"),TEXT("Update_MotionMatching"),TEXT("Update_MotionMatching_PostSelection"),TEXT("Get_MMBlendTime"),TEXT("Get_MMInterruptMode"),TEXT("Get_MMNotifyRecencyTimeOut"),TEXT("OnStateEntry_SlideTransition"),TEXT("OnStateEntry_SlideLoop"),TEXT("OnStateEntry_DebugFlyingLoop"),TEXT("Get_SlideSlopeRotation"),TEXT("Get_SlideSlopeOffset"),TEXT("Update_InteractionConstraints")};
 for (FName Name:Functions)
  if (auto* Graph=FindGraph(Target,Name))
  {
   if (FBlueprintEditorUtils::IsFunctionUsed(Target,Name)) {Report.Add(TEXT("Retained referenced function: ")+Name.ToString());continue;}
   FBlueprintEditorUtils::RemoveGraph(Target,Graph,EGraphRemoveFlags::None);Report.Add(TEXT("Removed unused function: ")+Name.ToString());
  }
 if (auto* Macro=FindGraph(Target,TEXT("UpdateStateValues")))
 {
  TArray<UK2Node_MacroInstance*> Uses;FBlueprintEditorUtils::GetAllNodesOfClass(Target,Uses);
  if (!Uses.ContainsByPredicate([&](const auto* Node){return Node->GetMacroGraph()==Macro;}))
  {FBlueprintEditorUtils::RemoveGraph(Target,Macro,EGraphRemoveFlags::None);Report.Add(TEXT("Removed old state-update macro"));}
 }
 const FName Variables[]={TEXT("LocomotionSetup"),TEXT("NetworkRole"),TEXT("MMDatabaseLOD"),TEXT("MoverSamplingFrameRate"),TEXT("PreviousDesiredControllerYaw"),TEXT("CurrentDatabaseTags"),TEXT("CurrentSelectedAnim"),TEXT("CurrentSelectedDatabase"),TEXT("MM Search Cost"),TEXT("Search Cost"),TEXT("ValidDatabases"),TEXT("ValidAnims"),TEXT("DebugTransitions"),TEXT("TransitionHistory"),TEXT("PawnSpeedHistory"),TEXT("MoveData_Speed_History"),TEXT("Phase_History"),TEXT("Contact_L_History"),TEXT("Contact_R_History"),TEXT("Enable_Warping_History"),TEXT("Enable_MMI_IK"),TEXT("MMI_Constraints"),TEXT("MMI_IK_Hand L Alpha"),TEXT("MMI_IK_Hand L Target"),TEXT("MMI_IK_Hand R Alpha"),TEXT("MMI_IK_Hand R Target")};
 for (FName Name:Variables)
  if (Target->NewVariables.ContainsByPredicate([&](const auto& Variable){return Variable.VarName==Name;}) && !FBlueprintEditorUtils::IsVariableUsed(Target,Name))
  {FBlueprintEditorUtils::RemoveMemberVariable(Target,Name);Report.Add(TEXT("Removed retired storage: ")+Name.ToString());}
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FKismetEditorUtilities::CompileBlueprint(Target);
 Report.Add(Target->Status==BS_Error?TEXT("ERROR: Blueprint compile failed"):TEXT("COMPLETED"));
 return Report;
}

int32 UProject_JPlayerLocomotionLibrary::CleanDisconnectedLocomotionEvents(UBlueprint* Target)
{
 if (!Target) return 0;
 int32 Count=0;
 for (UEdGraph* Graph:Target->UbergraphPages) for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
 {
  const auto* Event=Cast<UK2Node_Event>(Node);
  const auto* Call=Cast<UK2Node_CallFunction>(Node);
  const bool bDefaultEvent=Event && (Event->EventReference.GetMemberName()==TEXT("ReceiveBeginPlay") || Event->EventReference.GetMemberName()==TEXT("ReceiveTick") || Event->EventReference.GetMemberName()==TEXT("ReceiveActorBeginOverlap") || Event->EventReference.GetMemberName()==TEXT("BlueprintUpdateAnimation"));
  const bool bUnusedOwnerRead=Call && Call->GetFunctionName()==TEXT("TryGetPawnOwner");
  if ((bDefaultEvent || bUnusedOwnerRead) && !Node->Pins.ContainsByPredicate([](const auto* Pin){return !Pin->LinkedTo.IsEmpty();}))
  {FBlueprintEditorUtils::RemoveNode(Target,Node,true);++Count;}
 }
 if (Count) {FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FKismetEditorUtilities::CompileBlueprint(Target);}
 return Count;
}

TArray<FString> UProject_JPlayerLocomotionLibrary::SimplifyNativeLocomotionQueries(UAnimBlueprint* Target,const TArray<UChooserTable*>& Choosers)
{
 if(!Target || !Target->GeneratedClass || !Target->GeneratedClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()) || !Target->NewVariables.IsEmpty() || (GEditor && GEditor->PlayWorld))return {TEXT("ERROR: Native state contract and stopped PIE required")};
 struct FAdapter {UFunction* Function;UEdGraph* Graph;TMap<FName,FName> Pins;};
 TMap<FName,FAdapter> Adapters;TArray<FString> Report;
 for(UEdGraph* Graph:Target->FunctionGraphs)
 {
  bool Interface=false;for(const auto& I:Target->ImplementedInterfaces)Interface|=I.Graphs.Contains(Graph);
  if(Interface)continue;
  UK2Node_CallFunction* Call=nullptr;UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;bool Other=false;
  for(UEdGraphNode* Node:Graph->Nodes)
  {
   if(auto* E=Cast<UK2Node_FunctionEntry>(Node))Entry=E;
   else if(auto* R=Cast<UK2Node_FunctionResult>(Node))Result=R;
   else if(auto* C=Cast<UK2Node_CallFunction>(Node);C && !Call)Call=C;
   else if(Node->GetClass()->GetFName()!=TEXT("EdGraphNode_Comment"))Other=true;
  }
  UFunction* Function=Call?Call->GetTargetFunction():nullptr;
  if(Other || !Entry || !Result || !Function || Function->GetOwnerClass()!=UProject_JPlayerAnimInstance::StaticClass() || !Function->HasAllFunctionFlags(FUNC_Native|FUNC_BlueprintPure))continue;
  FAdapter Adapter{Function,Graph,{}};bool Simple=true;
  for(UEdGraphPin* Pin:Call->Pins)
  {
   if(Pin->PinType.PinCategory==UEdGraphSchema_K2::PC_Exec || Pin->PinName==UEdGraphSchema_K2::PN_Self)continue;
   if(Pin->LinkedTo.Num()!=1 || Pin->LinkedTo[0]->ParentPin){Simple=false;break;}
   UEdGraphPin* Link=Pin->LinkedTo[0];
   if((Pin->Direction==EGPD_Input && Link->GetOwningNode()!=Entry) || (Pin->Direction==EGPD_Output && Link->GetOwningNode()!=Result)){Simple=false;break;}
   Adapter.Pins.Add(Link->PinName,Pin->PinName);
  }
  if(Simple)Adapters.Add(Graph->GetFName(),MoveTemp(Adapter));
 }
 // Edit only function references and root binding segments, never row tags/curve names.
 TFunction<void(const UStruct*,void*,UObject*,int32)> Visit;
 TFunction<void(FProperty*,void*,UObject*,int32)> Value;
 TSet<UObject*> Seen;
 Value=[&](FProperty* P,void* Data,UObject* Scope,int32 Depth)
 {
  if(Depth>20)return;
  if(auto* S=CastField<FStructProperty>(P))
  {
   if(S->Struct==FMemberReference::StaticStruct())
   {auto& Ref=*static_cast<FMemberReference*>(Data);if(auto* A=Adapters.Find(Ref.GetMemberName()))Ref.SetExternalMember(A->Function->GetFName(),UProject_JPlayerAnimInstance::StaticClass());}
   else if(S->Struct==FInstancedStruct::StaticStruct())
   {auto& V=*static_cast<FInstancedStruct*>(Data);if(V.IsValid())Visit(V.GetScriptStruct(),V.GetMutableMemory(),Scope,Depth+1);}
   else Visit(S->Struct,Data,Scope,Depth+1);
  }
  else if(auto* A=CastField<FArrayProperty>(P))
  {
   FScriptArrayHelper Array(A,Data);
   if((P->GetFName()==TEXT("PropertyPath") || P->GetFName()==TEXT("PropertyBindingChain")) && Array.Num())
   {
    if(auto* N=CastField<FNameProperty>(A->Inner)){const FName Old=N->GetPropertyValue(Array.GetRawPtr(0));if(auto* Q=Adapters.Find(Old))N->SetPropertyValue(Array.GetRawPtr(0),Q->Function->GetFName());}
    if(auto* String=CastField<FStrProperty>(A->Inner)){const FString Old=String->GetPropertyValue(Array.GetRawPtr(0));if(auto* Q=Adapters.Find(FName(Old)))String->SetPropertyValue(Array.GetRawPtr(0),Q->Function->GetName());}
   }
   else for(int32 I=0;I<Array.Num();++I)Value(A->Inner,Array.GetRawPtr(I),Scope,Depth+1);
  }
  else if(auto* M=CastField<FMapProperty>(P))
  {FScriptMapHelper Map(M,Data);for(int32 I=0;I<Map.GetMaxIndex();++I)if(Map.IsValidIndex(I))Value(M->ValueProp,Map.GetValuePtr(I),Scope,Depth+1);}
  else if(auto* O=CastField<FObjectPropertyBase>(P))
  {UObject* Object=O->GetObjectPropertyValue(Data);if(Object && Object->IsIn(Scope) && !Object->IsA<UEdGraph>() && !Object->IsA<UEdGraphNode>() && !Seen.Contains(Object)){Seen.Add(Object);Visit(Object->GetClass(),Object,Scope,Depth+1);}}
 };
 Visit=[&](const UStruct* Type,void* Data,UObject* Scope,int32 Depth){for(TFieldIterator<FProperty> P(Type);P;++P)Value(*P,P->ContainerPtrToValuePtr<void>(Data),Scope,Depth);};
 Target->Modify();TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);
 for(auto* Graph:Graphs)
 {
  if(Adapters.Contains(Graph->GetFName()))continue;
  for(UEdGraphNode* Node:Graph->Nodes)
  {
   if(auto* Call=Cast<UK2Node_CallFunction>(Node))if(auto* Adapter=Adapters.Find(Call->FunctionReference.GetMemberName()))
   {
    for(auto* Pin:Call->Pins)
    {
     FString Name=Pin->PinName.ToString();
     for(const auto& Pair:Adapter->Pins)if(Name==Pair.Key.ToString() || Name.StartsWith(Pair.Key.ToString()+TEXT("_"))){Name=Pair.Value.ToString()+Name.Mid(Pair.Key.ToString().Len());break;}
     Pin->PinName=FName(Name);
    }
    Call->SetFromFunction(Adapter->Function);Call->ReconstructNode();
   }
   if(!Cast<UK2Node_FunctionEntry>(Node) && !Cast<UK2Node_FunctionResult>(Node))Visit(Node->GetClass(),Node,Node,0);
  }
 }
 for(auto* Table:Choosers)if(Table){Table->Modify();Visit(Table->GetClass(),Table,Table,0);Table->MarkPackageDirty();}
 for(const auto& Pair:Adapters)
 {Report.Add(TEXT("Native query: ")+Pair.Key.ToString()+TEXT(" -> ")+Pair.Value.Function->GetName());FBlueprintEditorUtils::RemoveGraph(Target,Pair.Value.Graph,EGraphRemoveFlags::None);}
 if(auto* Pre=FindGraph(Target,TEXT("Procedural_PreLayering")))
 {
  UAnimGraphNode_LinkedInputPose* Input=nullptr;UAnimGraphNode_Root* Root=nullptr;bool Other=false;
  for(UEdGraphNode* Node:Pre->Nodes)
  {if(auto* I=Cast<UAnimGraphNode_LinkedInputPose>(Node))Input=I;else if(auto* R=Cast<UAnimGraphNode_Root>(Node))Root=R;else Other=true;}
  if(!Other && Input && Root && Root->FindPin(TEXT("Result")) && Root->FindPin(TEXT("Result"))->LinkedTo.Num()==1 && Root->FindPin(TEXT("Result"))->LinkedTo[0]->GetOwningNode()==Input)
  {
   for(auto* Graph:Graphs)if(Graph!=Pre && !Adapters.Contains(Graph->GetFName()))for(UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))if(auto* Layer=Cast<UAnimGraphNode_LinkedAnimLayer>(Node);Layer && Layer->Node.Layer==TEXT("Procedural_PreLayering") && !Layer->Node.Interface)
   {
    UEdGraphPin* Before=nullptr;auto* Output=Layer->FindPin(TEXT("Pose"));
    for(auto* Pin:Layer->Pins)if(Pin->Direction==EGPD_Input && Pin->PinType.PinSubCategoryObject==FPoseLink::StaticStruct() && Pin->LinkedTo.Num()==1)Before=Pin->LinkedTo[0];
    if(!Before || !Output)return {TEXT("ERROR: Unexpected pre-layering pose contract")};
    const auto Consumers=Output->LinkedTo;Layer->BreakAllNodeLinks();
    for(auto* Consumer:Consumers)if(!Graph->GetSchema()->TryCreateConnection(Before,Consumer))return {TEXT("ERROR: Cannot bypass empty pre-layer")};
    FBlueprintEditorUtils::RemoveNode(Target,Layer,true);
   }
   FBlueprintEditorUtils::RemoveGraph(Target,Pre,EGraphRemoveFlags::None);Report.Add(TEXT("Removed internal no-op pre-layering pose hop"));
  }
 }
 // An internal helper duplicated the interface's pose-history query; native selection
 // now queries the tagged collector directly. Preserve the external interface method.
 if(auto* Helper=FindGraph(Target,TEXT("Get_PoseHistoryReference"));Helper && !FBlueprintEditorUtils::IsFunctionUsed(Target,Helper->GetFName()))
 {FBlueprintEditorUtils::RemoveGraph(Target,Helper,EGraphRemoveFlags::None);Report.Add(TEXT("Removed unused internal pose-history helper"));}
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FCompilerResultsLog Compiler;Compiler.bSilentMode=true;FKismetEditorUtilities::CompileBlueprint(Target,EBlueprintCompileOptions::SkipGarbageCollection,&Compiler);
 for(const auto& Message:Compiler.Messages)if(Message->GetSeverity()<=EMessageSeverity::Warning)Report.Add(TEXT("ERROR: ")+Message->ToText().ToString());
 for(auto* Table:Choosers)if(Table)Table->Compile(true);
 Report.Add(Target->Status==BS_Error?TEXT("ERROR: Blueprint compile failed"):TEXT("COMPLETED"));return Report;
}

bool UProject_JPlayerLocomotionLibrary::UseNativeRetargetWristTarget(UAnimBlueprint* Target)
{
 if(!Target || !Target->GeneratedClass || !Target->GeneratedClass->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()) || (GEditor && GEditor->PlayWorld))return false;
 TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);int32 Replaced=0;
 for(auto* Graph:Graphs)for(UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))if(auto* Call=Cast<UK2Node_CallFunction>(Node);Call && Call->GetFunctionName()==TEXT("MakeTransform"))
 {
  auto* Location=Call->FindPin(TEXT("Location"));auto* Rotation=Call->FindPin(TEXT("Rotation"));auto* Scale=Call->FindPin(TEXT("Scale"));auto* Output=Call->GetReturnValuePin();
  if(!Location || !Rotation || !Scale || !Output || Location->LinkedTo.Num()!=1 || Rotation->LinkedTo.Num()!=1 || !Scale->LinkedTo.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("Wrist replacement: unexpected pin links"));return false;}
  auto* L=Cast<UK2Node_VariableGet>(Location->LinkedTo[0]->GetOwningNode());auto* R=Cast<UK2Node_VariableGet>(Rotation->LinkedTo[0]->GetOwningNode());FVector ScaleValue;
  if(!L || !R || L->VariableReference.GetMemberName()!=GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightGripLocation) || R->VariableReference.GetMemberName()!=GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightGripRotation) || !FDefaultValueHelper::ParseVector(Scale->DefaultValue,ScaleValue) || !ScaleValue.Equals(FVector::OneVector)){UE_LOG(LogTemp,Error,TEXT("Wrist replacement: unexpected source or scale %s"),*Scale->DefaultValue);return false;}
  Target->Modify();FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);auto* Native=Creator.CreateNode();Native->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(UProject_JRetargetAnimInstance,RightWristTarget));Creator.Finalize();Native->NodePosX=Call->NodePosX;Native->NodePosY=Call->NodePosY;
  const auto Connection=Graph->GetSchema()->MovePinLinks(*Output,*Native->GetValuePin(),true);
  if(!Connection.CanSafeConnect()){UE_LOG(LogTemp,Error,TEXT("Wrist replacement: %s"),*Connection.Message.ToString());return false;}
  FBlueprintEditorUtils::RemoveNode(Target,Call,true);
  for(auto* Old:{L,R})if(Old->GetValuePin()->LinkedTo.IsEmpty())FBlueprintEditorUtils::RemoveNode(Target,Old,true);
  ++Replaced;
 }
 if(!Replaced)return true;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FCompilerResultsLog Compiler;Compiler.bSilentMode=true;FKismetEditorUtilities::CompileBlueprint(Target,EBlueprintCompileOptions::SkipGarbageCollection,&Compiler);Target->MarkPackageDirty();for(const auto& Message:Compiler.Messages)if(Message->GetSeverity()<=EMessageSeverity::Warning)UE_LOG(LogTemp,Error,TEXT("Wrist compile: %s"),*Message->ToText().ToString());return Compiler.NumErrors==0 && Compiler.NumWarnings==0 && Target->Status!=BS_Error;
}
