#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "AnimGraphNode_TransitionResult.h"
#include "K2Node_AnimGetter.h"
#include "Editor.h"
#include "AnimPose.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/Actor.h"
#include "AnimationRuntime.h"
#include "Animation/AnimClassInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Engine/MemberReference.h"

FString UProject_JPlayerLocomotionLibrary::ExportNativeLocomotionContract(UBlueprint* Target)
{
 if(!Target || !Target->GeneratedClass)return TEXT("{}");
 TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();
 Root->SetStringField(TEXT("asset"),Target->GetPathName());Root->SetStringField(TEXT("parent"),GetPathNameSafe(Target->ParentClass));
 TArray<TSharedPtr<FJsonValue>> Interfaces;
 for(const auto& Interface:Target->ImplementedInterfaces)Interfaces.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Interface.Interface)));
 Root->SetArrayField(TEXT("interfaces"),Interfaces);
 TSharedRef<FJsonObject> Structs=MakeShared<FJsonObject>(),Enums=MakeShared<FJsonObject>();
 TFunction<TSharedRef<FJsonObject>(const FProperty*,const void*)> Describe;
 TFunction<void(const UEnum*)> Enum=[&](const UEnum* Type)
 {
  if(!Type || Enums->HasField(Type->GetPathName()))return;
  TArray<TSharedPtr<FJsonValue>> Entries;
  for(int32 Index=0;Index<Type->NumEnums();++Index)
  {
   auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("name"),Type->GetNameStringByIndex(Index));E->SetStringField(TEXT("display"),Type->GetDisplayNameTextByIndex(Index).ToString());E->SetNumberField(TEXT("value"),Type->GetValueByIndex(Index));Entries.Add(MakeShared<FJsonValueObject>(E));
  }
  Enums->SetArrayField(Type->GetPathName(),Entries);
 };
 Describe=[&](const FProperty* P,const void* Data)
 {
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),P->GetName());Row->SetStringField(TEXT("cpp"),P->GetCPPType());Row->SetStringField(TEXT("kind"),P->GetClass()->GetName());
  if(Data){FString Value;P->ExportTextItem_Direct(Value,Data,nullptr,nullptr,PPF_None);Row->SetStringField(TEXT("default"),Value);}
  if(auto* S=CastField<FStructProperty>(P))
  {
   Row->SetStringField(TEXT("struct"),S->Struct->GetPathName());
   if(S->Struct->IsA<UUserDefinedStruct>() && !Structs->HasField(S->Struct->GetPathName()))
   {
    Structs->SetArrayField(S->Struct->GetPathName(),{});TArray<TSharedPtr<FJsonValue>> Fields;
    for(TFieldIterator<FProperty> F(S->Struct);F;++F)Fields.Add(MakeShared<FJsonValueObject>(Describe(*F,Data?F->ContainerPtrToValuePtr<void>(Data):nullptr)));
    Structs->SetArrayField(S->Struct->GetPathName(),Fields);
   }
  }
  if(auto* B=CastField<FByteProperty>(P);B && B->Enum){Enum(B->Enum);Row->SetStringField(TEXT("enum"),B->Enum->GetPathName());}
  if(auto* E=CastField<FEnumProperty>(P)){Enum(E->GetEnum());Row->SetStringField(TEXT("enum"),E->GetEnum()->GetPathName());}
  if(auto* A=CastField<FArrayProperty>(P))Row->SetObjectField(TEXT("inner"),Describe(A->Inner,nullptr));
  return Row;
 };
 TArray<TSharedPtr<FJsonValue>> Variables;
 for(const auto& Variable:Target->NewVariables)if(const auto* P=FindFProperty<FProperty>(Target->GeneratedClass,Variable.VarName))
 {
  auto Row=Describe(P,P->ContainerPtrToValuePtr<void>(Target->GeneratedClass->GetDefaultObject()));Row->SetStringField(TEXT("category"),Variable.Category.ToString());Variables.Add(MakeShared<FJsonValueObject>(Row));
 }
 Root->SetArrayField(TEXT("variables"),Variables);Root->SetObjectField(TEXT("structs"),Structs);Root->SetObjectField(TEXT("enums"),Enums);
 TArray<TSharedPtr<FJsonValue>> NativeVariables;
 if(Target->GeneratedClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()))
  for(TFieldIterator<FProperty> P(UProject_JPlayerAnimInstance::StaticClass(),EFieldIteratorFlags::ExcludeSuper);P;++P)
   NativeVariables.Add(MakeShared<FJsonValueObject>(Describe(*P,P->ContainerPtrToValuePtr<void>(Target->GeneratedClass->GetDefaultObject()))));
 Root->SetArrayField(TEXT("native_variables"),NativeVariables);
 TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);TArray<TSharedPtr<FJsonValue>> GraphRows;
 for(auto* Graph:Graphs)
 {
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Graph->GetName());Row->SetNumberField(TEXT("nodes"),Graph->Nodes.Num());TArray<TSharedPtr<FJsonValue>> Nodes;
  if(Target->FunctionGraphs.Contains(Graph))Row->SetBoolField(TEXT("referenced"),FBlueprintEditorUtils::IsFunctionUsed(Target,Graph->GetFName()));
  for(UEdGraphNode* Node:Graph->Nodes)
  {
   auto N=MakeShared<FJsonObject>();N->SetStringField(TEXT("name"),Node->GetName());N->SetStringField(TEXT("class"),Node->GetClass()->GetName());
   if(auto* Call=Cast<UK2Node_CallFunction>(Node))N->SetStringField(TEXT("function"),Call->FunctionReference.GetMemberName().ToString());
   if(auto* Var=Cast<UK2Node_Variable>(Node))N->SetStringField(TEXT("variable"),Var->VariableReference.GetMemberName().ToString());
   // Inspect authored callbacks/property-access bindings as well as visible call pins.
   // Follow only this node's owned binding objects, never referenced asset graphs.
   TArray<TSharedPtr<FJsonValue>> Bindings;TSet<const UObject*> Seen;
   TFunction<void(const FProperty*,const void*,const FString&,int32)> Inspect;
   TFunction<void(const UStruct*,const void*,const FString&,int32)> InspectStruct;
   InspectStruct=[&](const UStruct* Type,const void* Data,const FString& Path,int32 Depth)
   {if(Depth>16)return;for(TFieldIterator<FProperty> P(Type);P;++P)Inspect(*P,P->ContainerPtrToValuePtr<void>(Data),Path+TEXT(".")+P->GetName(),Depth+1);};
   Inspect=[&](const FProperty* P,const void* Data,const FString& Path,int32 Depth)
   {
    if(Depth>16)return;
    if(const auto* S=CastField<FStructProperty>(P))
    {
     if(S->Struct==FMemberReference::StaticStruct())
     {const auto& Ref=*static_cast<const FMemberReference*>(Data);if(!Ref.GetMemberName().IsNone()){auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("property"),Path);B->SetStringField(TEXT("member"),Ref.GetMemberName().ToString());B->SetStringField(TEXT("parent"),GetPathNameSafe(Ref.GetMemberParentClass()));Bindings.Add(MakeShared<FJsonValueObject>(B));}}
     else InspectStruct(S->Struct,Data,Path,Depth);
    }
    else if(const auto* A=CastField<FArrayProperty>(P))
    {
     FScriptArrayHelper Values(A,Data);
     if(P->GetFName()==TEXT("PropertyPath") || P->GetFName()==TEXT("Path"))
     {
      TArray<TSharedPtr<FJsonValue>> Segments;
      for(int32 I=0;I<Values.Num();++I){FString Value;A->Inner->ExportTextItem_Direct(Value,Values.GetRawPtr(I),nullptr,nullptr,PPF_None);Segments.Add(MakeShared<FJsonValueString>(Value));}
      if(!Segments.IsEmpty()){auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("property"),Path);B->SetArrayField(TEXT("chain"),Segments);Bindings.Add(MakeShared<FJsonValueObject>(B));}
     }
     else for(int32 I=0;I<Values.Num();++I)Inspect(A->Inner,Values.GetRawPtr(I),Path+FString::Printf(TEXT("[%d]"),I),Depth+1);
    }
    else if(const auto* M=CastField<FMapProperty>(P))
    {FScriptMapHelper Values(M,Data);for(int32 I=0;I<Values.GetMaxIndex();++I)if(Values.IsValidIndex(I)){FString Key;M->KeyProp->ExportTextItem_Direct(Key,Values.GetKeyPtr(I),nullptr,nullptr,PPF_None);Inspect(M->ValueProp,Values.GetValuePtr(I),Path+TEXT("[")+Key+TEXT("]"),Depth+1);}}
    else if(const auto* O=CastField<FObjectPropertyBase>(P))
    {const auto* Object=O->GetObjectPropertyValue(Data);if(Object && Object->IsIn(Node) && !Object->IsA<UEdGraph>() && !Object->IsA<UEdGraphNode>() && !Seen.Contains(Object)){Seen.Add(Object);InspectStruct(Object->GetClass(),Object,Path,Depth+1);}}
    else if(const auto* Name=CastField<FNameProperty>(P);Name && (P->GetFName()==TEXT("Layer") || P->GetFName()==TEXT("CacheName") || P->GetFName()==TEXT("Tag") || P->GetFName()==TEXT("SlotName")))
    {auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("property"),Path);B->SetStringField(TEXT("value"),Name->GetPropertyValue(Data).ToString());Bindings.Add(MakeShared<FJsonValueObject>(B));}
   };
   InspectStruct(Node->GetClass(),Node,TEXT("Node"),0);N->SetArrayField(TEXT("bindings"),Bindings);
   TArray<TSharedPtr<FJsonValue>> PinRows;
   for(UEdGraphPin* Pin:Node->Pins)
   {
    auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("id"),Pin->PinId.ToString());P->SetStringField(TEXT("name"),Pin->PinName.ToString());P->SetStringField(TEXT("default"),Pin->DefaultValue);P->SetNumberField(TEXT("direction"),Pin->Direction);
    P->SetStringField(TEXT("category"),Pin->PinType.PinCategory.ToString());if(Pin->DefaultObject)P->SetStringField(TEXT("default_object"),Pin->DefaultObject->GetPathName());if(Pin->PinType.PinSubCategoryObject.IsValid())P->SetStringField(TEXT("type"),Pin->PinType.PinSubCategoryObject->GetPathName());
    TArray<TSharedPtr<FJsonValue>> Links;for(auto* Link:Pin->LinkedTo){auto L=MakeShared<FJsonObject>();L->SetStringField(TEXT("node"),Link->GetOwningNode()->GetName());L->SetStringField(TEXT("pin"),Link->PinName.ToString());Links.Add(MakeShared<FJsonValueObject>(L));}P->SetArrayField(TEXT("links"),Links);PinRows.Add(MakeShared<FJsonValueObject>(P));
   }
   N->SetArrayField(TEXT("pins"),PinRows);
   Nodes.Add(MakeShared<FJsonValueObject>(N));
  }
  Row->SetArrayField(TEXT("node_details"),Nodes);Row->SetStringField(TEXT("path"),Graph->GetPathName());GraphRows.Add(MakeShared<FJsonValueObject>(Row));
 }
 Root->SetArrayField(TEXT("graphs"),GraphRows);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));return Json;
}

bool UProject_JPlayerLocomotionLibrary::InstallNativeLocomotionPins(UAnimBlueprint* Target)
{
 if(!Target || !Target->GeneratedClass || !Target->GeneratedClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()) || !Target->NewVariables.IsEmpty() || (GEditor && GEditor->PlayWorld))return false;
 using RuleType=EProject_JPlayerTransitionRule;
 const TMap<FName,RuleType> Rules={
  {TEXT("AnimStateTransitionNode_0"),RuleType::Moving},{TEXT("AnimStateTransitionNode_1"),RuleType::Idle},
  {TEXT("AnimStateTransitionNode_18"),RuleType::AirTransitionComplete},{TEXT("AnimStateTransitionNode_22"),RuleType::NoValidAnimationOrLooping},
  {TEXT("AnimStateTransitionNode_4"),RuleType::StanceChanged},{TEXT("AnimStateTransitionNode_12"),RuleType::NewPivot},
  {TEXT("AnimStateTransitionNode_19"),RuleType::LocomotionSettingsChanged},{TEXT("AnimStateTransitionNode_20"),RuleType::TransitionPivot},
  {TEXT("AnimStateTransitionNode_24"),RuleType::InAir},{TEXT("AnimStateTransitionNode_15"),RuleType::Grounded},
  {TEXT("AnimStateTransitionNode_26"),RuleType::IdleTurnRedirect},{TEXT("AnimStateTransitionNode_21"),RuleType::EarlyFacingRedirect},
  {TEXT("AnimStateTransitionNode_6"),RuleType::IdleBreakElapsed},{TEXT("AnimStateTransitionNode_16"),RuleType::Moving},
  {TEXT("AnimStateTransitionNode_17"),RuleType::Idle},{TEXT("AnimStateTransitionNode_23"),RuleType::NotifyRedirect},
  {TEXT("AnimStateTransitionNode_10"),RuleType::NotifyLoop},{TEXT("AnimStateTransitionNode_28"),RuleType::Circling},
  {TEXT("AnimStateTransitionNode_25"),RuleType::TrajectoryDiscontinuity},{TEXT("AnimStateTransitionNode_29"),RuleType::MovementDirectionChanged},
  {TEXT("AnimStateTransitionNode_31"),RuleType::SlopeChanged}
 };
 TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);int32 Installed=0;
 for(auto* Graph:Graphs)
 {
  if(!Graph->GetPathName().Contains(TEXT(".State Controller.")))continue;
  const auto* Rule=Rules.Find(Graph->GetOuter()->GetFName());if(!Rule)continue;
  UAnimGraphNode_TransitionResult* Result=nullptr;UK2Node_AnimGetter* Clock=nullptr;
  for(UEdGraphNode* Node:Graph->Nodes)
  {
   if(auto* R=Cast<UAnimGraphNode_TransitionResult>(Node))Result=R;
   if(auto* G=Cast<UK2Node_AnimGetter>(Node))
   {
    if(G->FunctionReference.GetMemberName()!=TEXT("GetInstanceCurrentStateElapsedTime"))return false;
    if(!Clock)Clock=G; // All copies point at this transition's source state.
   }
  }
  if(!Result)continue;
  Graph->Modify();
  for(UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))if(Node!=Result && Node!=Clock)FBlueprintEditorUtils::RemoveNode(Target,Node,true);
  Result->BreakAllNodeLinks();
  FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Call=Creator.CreateNode();Call->SetFromFunction(UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateNativeTransition)));Creator.Finalize();
  const auto* Schema=Graph->GetSchema();Call->FindPinChecked(TEXT("Rule"))->DefaultValue=StaticEnum<RuleType>()->GetNameStringByValue(static_cast<int64>(*Rule));
  if(Clock && !Schema->TryCreateConnection(Clock->GetReturnValuePin(),Call->FindPin(TEXT("StateElapsedTime"))))return false;
  if(!Schema->TryCreateConnection(Call->GetReturnValuePin(),Result->FindPin(TEXT("bCanEnterTransition"))))return false;
  ++Installed;
 }
 if(Installed!=Rules.Num())return false;
 for(auto* Graph:Graphs)if(Graph->GetPathName().Contains(TEXT(".AnimationBlendStackGraph_")))
 {
  for(UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))if(auto* CurveCall=Cast<UK2Node_CallFunction>(Node);CurveCall && CurveCall->FunctionReference.GetMemberName()==TEXT("GetCurveValueFromAnimation"))
  {
   if(CurveCall->FindPinChecked(TEXT("CurveName"))->DefaultValue!=TEXT("Enable_TurnInPlaceSteering"))return false;
   auto* Time=CurveCall->FindPinChecked(TEXT("Time"));if(Time->LinkedTo.Num()!=1)return false;
   auto* TimeCall=Cast<UK2Node_CallFunction>(Time->LinkedTo[0]->GetOwningNode());if(!TimeCall)return false;
   UEdGraphPin* Reference=nullptr;for(auto* Pin:TimeCall->Pins)if(Pin->Direction==EGPD_Input && Pin->PinType.PinSubCategoryObject==FAnimNodeReference::StaticStruct())Reference=Pin;
   if(!Reference || Reference->LinkedTo.Num()!=1)return false;
   FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Call=Creator.CreateNode();Call->SetFromFunction(UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTurnInPlaceSteeringAlpha)));Creator.Finalize();
   const auto* Schema=Graph->GetSchema();if(!Schema->TryCreateConnection(Reference->LinkedTo[0],Call->FindPin(TEXT("Node"))))return false;
   if(!Schema->MovePinLinks(*CurveCall->FindPinChecked(TEXT("OutValue")),*Call->GetReturnValuePin(),true).CanSafeConnect())return false;
   FBlueprintEditorUtils::RemoveNode(Target,CurveCall,true);
   // Remove only unconsumed pure inputs in this graph, preserving player-specific references.
   bool Removed;
   do{Removed=false;for(UEdGraphNode* Candidate:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))if(auto* K2=Cast<UK2Node>(Candidate);K2 && K2->IsNodePure())
   {
    bool HasOutput=false,Consumed=false;for(auto* Pin:K2->Pins)if(Pin->Direction==EGPD_Output){HasOutput=true;Consumed|=!Pin->LinkedTo.IsEmpty();}
    if(HasOutput && !Consumed){FBlueprintEditorUtils::RemoveNode(Target,K2,true);Removed=true;}
   }}while(Removed);
  }
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FCompilerResultsLog Compiler;Compiler.bSilentMode=true;FKismetEditorUtilities::CompileBlueprint(Target,EBlueprintCompileOptions::SkipGarbageCollection,&Compiler);
 for(const auto& Message:Compiler.Messages)if(Message->GetSeverity()<=EMessageSeverity::Warning)UE_LOG(LogTemp,Error,TEXT("Native pin compilation: %s"),*Message->ToText().ToString());
 Target->MarkPackageDirty();return Compiler.NumErrors==0 && Compiler.NumWarnings==0 && Target->Status!=BS_Error;
}



FString UProject_JPlayerLocomotionLibrary::InspectVisualRetargetPose(UAnimInstance* Instance)
{
 check(IsInGameThread());
 const auto Result=MakeShared<FJsonObject>();
 auto* Mesh=Instance?Instance->GetSkelMeshComponent():nullptr;
 if(!Mesh || !Mesh->GetSkeletalMeshAsset())return TEXT("{}");
 Mesh->HandleExistingParallelEvaluationTask(true,true);
 const auto* Interface=IAnimClassInterface::GetFromClass(Instance->GetClass());
 if(!Interface)return TEXT("{}");
 for(auto* Property:Interface->GetAnimNodeProperties())
 {
  if(Property->Struct!=FAnimNode_RetargetPoseFromMesh::StaticStruct())continue;
  auto* Node=Property->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Instance);
  auto* Source=Node->SourceMeshComponent.Get();auto* Runtime=Node->GetRetargetProcessor();
  if(!Source || !Source->GetSkeletalMeshAsset() || !Runtime || !Runtime->IsInitialized())continue;
  Source->HandleExistingParallelEvaluationTask(true,true);
  Result->SetStringField(TEXT("retargeter"),GetPathNameSafe(Node->IKRetargeterAsset));
  Result->SetNumberField(TEXT("target_lod"),Mesh->GetPredictedLODLevel());
  Result->SetNumberField(TEXT("ik_lod_threshold"),Node->LODThresholdForIK);
  FString ProfileText;FRetargetProfile::StaticStruct()->ExportText(ProfileText,&Node->CustomRetargetProfile,nullptr,nullptr,PPF_None,nullptr);
  Result->SetStringField(TEXT("custom_profile"),ProfileText);
  Result->SetBoolField(TEXT("custom_override_sets"),Node->bUseCustomOverrideSets);
  FIKRetargetProcessor Fresh;FRetargetInitParameters Init;Init.SourceSkeletalMesh=Source->GetSkeletalMeshAsset();Init.TargetSkeletalMesh=Mesh->GetSkeletalMeshAsset();Init.RetargeterAsset=Node->IKRetargeterAsset;Fresh.Initialize(Init);
  if(!Fresh.IsInitialized())continue;
  TArray<FTransform> Input=Source->GetComponentSpaceTransforms();FRetargetRunParameters Run;Run.SourceGlobalPose=&Input;Run.OverrideSetsToApply=Node->IKRetargeterAsset->GetOverrideSetsToApply();
  const auto& Expected=Fresh.RunRetargeter(Run);
  const auto& Base=Runtime->GetTargetSkeleton().OutputGlobalPose;const auto& Final=Mesh->GetComponentSpaceTransforms();const auto& Skeleton=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
  auto Transform=[](const FTransform& T){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("position"),T.GetLocation().ToString());O->SetStringField(TEXT("rotation"),T.Rotator().ToString());return O;};
  TArray<TSharedPtr<FJsonValue>> Bones;
  for(int32 Index=0;Index<Skeleton.GetNum();++Index)
  {
   const FString Name=Skeleton.GetBoneName(Index).ToString();if(!Name.Contains(TEXT("UpperArm")) && !Name.Contains(TEXT("Forearm")) && !Name.EndsWith(TEXT("Hand")) && !Name.Contains(TEXT("Clavicle")))continue;
   if(!Base.IsValidIndex(Index)||!Final.IsValidIndex(Index)||!Expected.IsValidIndex(Index))continue;
   auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("bone"),Name);O->SetObjectField(TEXT("final"),Transform(Final[Index]));O->SetObjectField(TEXT("runtime_retarget"),Transform(Base[Index]));O->SetObjectField(TEXT("fresh_retarget"),Transform(Expected[Index]));
   O->SetNumberField(TEXT("post_rotation_error_degrees"),FMath::RadiansToDegrees(Base[Index].GetRotation().GetNormalized().AngularDistance(Final[Index].GetRotation().GetNormalized())));
   O->SetNumberField(TEXT("post_position_error_cm"),FVector::Distance(Base[Index].GetLocation(),Final[Index].GetLocation()));
   O->SetNumberField(TEXT("fresh_rotation_error_degrees"),FMath::RadiansToDegrees(Base[Index].GetRotation().GetNormalized().AngularDistance(Expected[Index].GetRotation().GetNormalized())));
   O->SetNumberField(TEXT("fresh_position_error_cm"),FVector::Distance(Base[Index].GetLocation(),Expected[Index].GetLocation()));Bones.Add(MakeShared<FJsonValueObject>(O));
  }
  Result->SetArrayField(TEXT("bones"),Bones);
  TArray<TSharedPtr<FJsonValue>> Modular;
  if(auto* Owner=Mesh->GetOwner())
  {
   TInlineComponentArray<USkeletalMeshComponent*> Parts(Owner);
   TArray<FTransform> BodyReference;FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton,Skeleton.GetRefBonePose(),BodyReference);
   for(auto* Part:Parts)
   {
    if(Part==Mesh || Part->LeaderPoseComponent.Get()!=Mesh || !Part->GetSkeletalMeshAsset())continue;
    const auto& Ref=Part->GetSkeletalMeshAsset()->GetRefSkeleton();TArray<FTransform> Reference;
    FAnimationRuntime::FillUpComponentSpaceTransforms(Ref,Ref.GetRefBonePose(),Reference);
    auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("component"),Part->GetName());P->SetStringField(TEXT("mesh"),Part->GetSkeletalMeshAsset()->GetPathName());P->SetStringField(TEXT("relative_transform"),Part->GetRelativeTransform().ToString());
    TArray<TSharedPtr<FJsonValue>> Roles;
    for(int32 I=0;I<Ref.GetNum();++I)
    {
     const FName Name=Ref.GetBoneName(I);if(!Name.ToString().Contains(TEXT("UpperArm")) && !Name.ToString().Contains(TEXT("Forearm")) && !Name.ToString().EndsWith(TEXT("Hand")))continue;
     const int32 J=Skeleton.FindBoneIndex(Name);if(J==INDEX_NONE)continue;
     auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("bone"),Name.ToString());R->SetNumberField(TEXT("bind_position_error_cm"),FVector::Distance(Reference[I].GetLocation(),BodyReference[J].GetLocation()));R->SetNumberField(TEXT("bind_rotation_error_degrees"),FMath::RadiansToDegrees(Reference[I].GetRotation().GetNormalized().AngularDistance(BodyReference[J].GetRotation().GetNormalized())));
     R->SetNumberField(TEXT("world_position_error_cm"),FVector::Distance(Part->GetSocketLocation(Name),Mesh->GetSocketLocation(Name)));Roles.Add(MakeShared<FJsonValueObject>(R));
    }
    P->SetArrayField(TEXT("arm_roles"),Roles);Modular.Add(MakeShared<FJsonValueObject>(P));
   }
  }
  Result->SetArrayField(TEXT("modular"),Modular);
 }
 FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}


FString UProject_JPlayerLocomotionLibrary::InspectRetargetAnimation(UIKRetargeter* Retargeter,UAnimSequence* Sequence,USkeletalMesh* Source,USkeletalMesh* Target,double Time)
{
 check(IsInGameThread());const auto Result=MakeShared<FJsonObject>();
 const auto Serialize=[&](){FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;};
 if(!Retargeter || !Sequence || !Source || !Target){Result->SetStringField(TEXT("error"),TEXT("Missing asset"));return Serialize();}
 Result->SetStringField(TEXT("sequence"),Sequence->GetPathName());Result->SetBoolField(TEXT("source_skeleton_matches"),Sequence->GetSkeleton()==Source->GetSkeleton());Result->SetStringField(TEXT("source_mesh"),Source->GetPathName());Result->SetStringField(TEXT("target_mesh"),Target->GetPathName());Result->SetNumberField(TEXT("time"),Time);
 FAnimPoseEvaluationOptions Options;Options.OptionalSkeletalMesh=Source;Options.EvaluationType=EAnimDataEvalType::Compressed;Options.bIncorporateRootMotionIntoPose=false;
 FAnimPose Pose;UAnimPoseExtensions::GetAnimPoseAtTime(Sequence,Time,Options,Pose);
 if(!Pose.IsValid()){Result->SetStringField(TEXT("error"),TEXT("Sequence cannot evaluate on the selected source mesh"));return Serialize();}
 TArray<FTransform> Input;const auto& SourceRef=Source->GetRefSkeleton();
 for(int32 I=0;I<SourceRef.GetNum();++I)Input.Add(UAnimPoseExtensions::GetBonePose(Pose,SourceRef.GetBoneName(I),EAnimPoseSpaces::World));
 FIKRetargetProcessor Processor;FRetargetInitParameters Init;Init.SourceSkeletalMesh=Source;Init.TargetSkeletalMesh=Target;Init.RetargeterAsset=Retargeter;Processor.Initialize(Init);
 if(!Processor.IsInitialized()){Result->SetStringField(TEXT("error"),TEXT("Retarget processor failed to initialize"));return Serialize();}
 FRetargetRunParameters Run;Run.SourceGlobalPose=&Input;Run.OverrideSetsToApply=Retargeter->GetOverrideSetsToApply();const auto& Output=Processor.RunRetargeter(Run);
 TArray<TSharedPtr<FJsonValue>> Bones;const auto& Ref=Target->GetRefSkeleton();
 for(int32 I=0;I<Output.Num();++I)
 {
  auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("name"),Ref.GetBoneName(I).ToString());const auto& T=Output[I];const auto& Q=T.GetRotation();const auto V=T.GetLocation();
  B->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)});
  B->SetArrayField(TEXT("rotation"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});Bones.Add(MakeShared<FJsonValueObject>(B));
 }
 Result->SetArrayField(TEXT("bones"),Bones);return Serialize();
}


int32 UProject_JPlayerLocomotionLibrary::AlignOneToOneRetargetChains(UIKRetargeter* Retargeter,const TArray<FName>& TargetChains)
{
 check(IsInGameThread());
 if(!Retargeter || TargetChains.IsEmpty() || (GEditor && GEditor->PlayWorld))return INDEX_NONE;
 auto* Controller=UIKRetargeterController::GetController(Retargeter);
 FIKRetargetProcessor Processor;FRetargetInitParameters Init;Init.SourceSkeletalMesh=Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Source);Init.TargetSkeletalMesh=Retargeter->GetPreviewMesh(ERetargetSourceOrTarget::Target);Init.RetargeterAsset=Retargeter;Processor.Initialize(Init);
 if(!Processor.IsInitialized() || !Controller)return INDEX_NONE;
 const auto* TargetRig=Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
 TArray<TPair<TArray<int32>,TArray<int32>>> Pairs;
 TSet<int32> UniqueSourceBones;
 for(const FName TargetChain:TargetChains)
 {
  bool OneToOne=false;
  for(const auto& OpStruct:Retargeter->GetRetargetOps())if(const auto* Op=OpStruct.GetPtr<FIKRetargetFKChainsOp>())
   for(const auto& Settings:Op->Settings.ChainsToRetarget)if(Settings.TargetChainName==TargetChain)OneToOne=Settings.EnableFK && Settings.RotationMode==EFKChainRotationMode::OneToOne && Op->Settings.IKRigAsset==TargetRig;
  const FName SourceChain=Controller->GetSourceChain(TargetChain);
  const auto* A=Processor.GetBoneChains().GetResolvedBoneChainByName(SourceChain,ERetargetSourceOrTarget::Source);
  const auto* B=Processor.GetBoneChains().GetResolvedBoneChainByName(TargetChain,ERetargetSourceOrTarget::Target,TargetRig);
  if(!OneToOne || !A || !B || A->BoneIndices.Num()<2 || A->BoneIndices.Num()!=B->BoneIndices.Num())return INDEX_NONE;
  for(int32 I=0;I+1<A->BoneIndices.Num();++I)if(UniqueSourceBones.Contains(A->BoneIndices[I]))return INDEX_NONE;else UniqueSourceBones.Add(A->BoneIndices[I]);
  Pairs.Emplace(A->BoneIndices,B->BoneIndices);
 }
 // The built-in ChainToChain auto alignment compares normalized arc lengths.
 // OneToOne playback instead pairs joint ordinals, so align the same segments.
 const auto& Ref=Init.SourceSkeletalMesh->GetRefSkeleton();int32 Changed=0;
 for(const auto& Pair:Pairs)for(int32 I=0;I+1<Pair.Key.Num();++I)
 {
  const auto& SourcePose=Processor.GetSkeleton(ERetargetSourceOrTarget::Source).RetargetPoses.GetGlobalRetargetPose();
  const auto& TargetPose=Processor.GetSkeleton(ERetargetSourceOrTarget::Target).RetargetPoses.GetGlobalRetargetPose();
  const int32 Bone=Pair.Key[I];const FVector From=(SourcePose[Pair.Key[I+1]].GetLocation()-SourcePose[Bone].GetLocation()).GetSafeNormal();
  const FVector To=(TargetPose[Pair.Value[I+1]].GetLocation()-TargetPose[Pair.Value[I]].GetLocation()).GetSafeNormal();
  if(From.IsNearlyZero() || To.IsNearlyZero())return INDEX_NONE;
  const FQuat Delta=FQuat::FindBetweenNormals(From,To).GetNormalized();if(Delta.AngularDistance(FQuat::Identity)<1.e-6)continue;
  const int32 Parent=Ref.GetParentIndex(Bone);const FTransform Base=Ref.GetRefBonePose()[Bone]*(Parent==INDEX_NONE?FTransform::Identity:SourcePose[Parent]);
  const FQuat Offset=(Base.GetRotation().Inverse()*Delta*SourcePose[Bone].GetRotation()).GetNormalized();
  Controller->SetRotationOffsetForRetargetPoseBone(Ref.GetBoneName(Bone),Offset,ERetargetSourceOrTarget::Source);
  Processor.UpdateRetargetPoseAtRuntime(Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source),ERetargetSourceOrTarget::Source);++Changed;
 }
 return Changed;
}
