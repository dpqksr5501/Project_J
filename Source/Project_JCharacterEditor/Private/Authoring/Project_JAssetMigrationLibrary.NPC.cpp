#include "Authoring/Project_JAssetMigrationLibrary.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_ControlRig.h"
#include "AnimGraphNode_Slot.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "BlueprintEditorLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "String/LexFromString.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJNPCAnimationMigration,Log,All);
namespace
{
const TMap<FName,int32>& ExpectedNPCReads138()
{
 static const TMap<FName,int32> Values={{TEXT("GroundSpeed"),1},{TEXT("Direction"),1},{TEXT("IsFalling"),4},{TEXT("ShouldMove"),3},{TEXT("Velocity"),1},{TEXT("Character"),0},{TEXT("MovementComponent"),0}};
 return Values;
}
struct FNPCGraphRecipe138
{
 UEdGraph* Event=nullptr;
 TArray<UEdGraph*> Graphs;
 TArray<UK2Node_VariableGet*> Reads;
 TMap<FName,int32> Counts;
 UFunction* Greater=nullptr;
 FString Threshold;
 FEdGraphPinType ThresholdType;
 double ThresholdValue=0;
};
bool IsReal138(const UEdGraphPin* Pin)
{
 return Pin && !Pin->bOrphanedPin && Pin->PinType.PinCategory==UEdGraphSchema_K2::PC_Real &&
  (Pin->PinType.PinSubCategory==UEdGraphSchema_K2::PC_Float || Pin->PinType.PinSubCategory==UEdGraphSchema_K2::PC_Double);
}
const UK2Node_CallFunction* LinkedCall138(const UEdGraphPin* Pin)
{
 return Pin && Pin->LinkedTo.Num()==1 ? Cast<UK2Node_CallFunction>(Pin->LinkedTo[0]->GetOwningNode()):nullptr;
}
bool Function138(const UK2Node_CallFunction* Node,FName Name,UClass* Owner)
{
 const auto* Function=Node?Node->GetTargetFunction():nullptr;
 return Function && Function->GetFName()==Name && Function->GetOuterUClass()==Owner;
}
// The observed FVector reader is split XYZ with only Z consumed. A new native
// FVector return must retain that shape, not pretend the unlinked parent is used.
bool VelocityLeaves138(UEdGraphPin* Root,TArray<UEdGraphPin*>& Leaves,bool bOld,FString& Detail)
{
 Leaves.Reset();Detail=FString::Printf(TEXT("root_links=%d subpins=%d"),Root?Root->LinkedTo.Num():-1,Root?Root->SubPins.Num():-1);
 if(!Root || Root->Direction!=EGPD_Output || Root->bOrphanedPin || Root->PinType.PinCategory!=UEdGraphSchema_K2::PC_Struct || Root->PinType.PinSubCategoryObject.Get()!=TBaseStructure<FVector>::Get() || !Root->LinkedTo.IsEmpty() || Root->SubPins.Num()!=3)return false;
 const TCHAR* Axes[]={TEXT("X"),TEXT("Y"),TEXT("Z")};
 for(int32 Index=0;Index<3;++Index)
 {
  const FName Expected(*(Root->PinName.ToString()+TEXT("_")+Axes[Index]));UEdGraphPin* Found=nullptr;
  for(auto* Child:Root->SubPins)if(Child && Child->PinName==Expected){if(Found)return false;Found=Child;}
  if(!Found)return false;
  Detail+=FString::Printf(TEXT(" %s{links=%d,type=%s/%s,default=%s}"),*Found->PinName.ToString(),Found->LinkedTo.Num(),*Found->PinType.PinCategory.ToString(),*Found->PinType.PinSubCategory.ToString(),*Found->DefaultValue);
  if(Found->ParentPin!=Root || Found->GetOwningNode()!=Root->GetOwningNode() || Found->Direction!=EGPD_Output || !IsReal138(Found) || Found->PinType.PinSubCategory!=UEdGraphSchema_K2::PC_Double || !Found->SubPins.IsEmpty() || Found->DefaultObject || !Found->DefaultTextValue.IsEmpty() || Found->LinkedTo.Num()!=(bOld && Index==2?1:0))return false;
  Leaves.Add(Found);
 }
 return true;
}
bool CollectNPCRecipe138(UAnimBlueprint* BP,FNPCGraphRecipe138& R,FString& Error)
{
 const auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(!BP || !BP->GeneratedClass || !BP->TargetSkeleton)return Fail(TEXT("Template lacks compiled animation class or target skeleton"));
 const auto* Interface=IAnimClassInterface::GetFromClass(BP->GeneratedClass);
 if(!Interface || Interface->GetAnimNodeProperties().IsEmpty() || Interface->GetAnimNodeProperties().Num()>4096)return Fail(TEXT("Expected a bounded compiled animation graph"));
 for(const auto* Property:Interface->GetAnimNodeProperties())
  if(!Property || !Property->Struct || Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()) || Property->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct()) || Property->Struct->IsChildOf(FAnimNode_LinkedAnimGraph::StaticStruct()))return Fail(TEXT("Selected source recipe requires no PoseSearch or linked/dynamic animation implementations"));
 BP->GetAllGraphs(R.Graphs);if(R.Graphs.IsEmpty() || R.Graphs.Num()>32)return Fail(TEXT("Selected graph count is outside1..32"));
 int32 Nodes=0;UK2Node_VariableSet* ShouldWriter=nullptr;
 for(auto* Graph:R.Graphs)
 {
  if(!Graph || (Nodes+=Graph->Nodes.Num())>2048)return Fail(TEXT("Invalid graph or more than2048 nodes"));
  if(Graph->GetFName()==TEXT("EventGraph")){if(R.Event)return Fail(TEXT("Ambiguous EventGraph"));R.Event=Graph;}
 }
 if(!R.Event)return Fail(TEXT("Selected template has no EventGraph"));
 for(auto* Graph:R.Graphs)for(const auto& Object:Graph->Nodes)
 {
  auto* Node=Object.Get();if(!Node)return Fail(TEXT("Null authored node"));
  if(auto* Var=Cast<UK2Node_Variable>(Node))
  {
   const FName Name=Var->GetVarName();
   if(Graph==R.Event)
   {
    if(Name==TEXT("ShouldMove"))if(auto* Set=Cast<UK2Node_VariableSet>(Var)){if(ShouldWriter)return Fail(TEXT("Multiple ShouldMove writers"));ShouldWriter=Set;}
   }
   else if(ExpectedNPCReads138().Contains(Name))
   {
    auto* Get=Cast<UK2Node_VariableGet>(Var);
    auto* Output=Get?Get->GetValuePin():nullptr;const bool bPure=Get && Get->IsNodePure(),bSelf=Var->VariableReference.IsSelfContext();FString Shape;
    TArray<UEdGraphPin*> VelocityLeaves;
    const bool bConsumed=Output && (Name==TEXT("Velocity") && !Output->SubPins.IsEmpty() ? VelocityLeaves138(Output,VelocityLeaves,true,Shape) : Output->SubPins.IsEmpty() && !Output->LinkedTo.IsEmpty());
    if(!Get || !bPure || !bSelf || !Output || !bConsumed)
    {
     Error=FString::Printf(TEXT("Unexpected old-variable reader: node=%s variable=%s get=%d pure=%d self=%d value=%d root_links=%d subpins=%d consumed=%d shape=%s"),*Node->GetPathName(),*Name.ToString(),Get!=nullptr,bPure,bSelf,Output!=nullptr,Output?Output->LinkedTo.Num():-1,Output?Output->SubPins.Num():-1,bConsumed,*Shape);return false;
    }
    const auto* Property=Var->GetPropertyForVariable();if(!Property)return Fail(TEXT("Retired variable property is unresolved"));
    if((Name==TEXT("GroundSpeed") || Name==TEXT("Direction")) && !CastField<FFloatProperty>(Property) && !CastField<FDoubleProperty>(Property))return Fail(TEXT("Old speed/direction is not a real scalar"));
    if((Name==TEXT("ShouldMove") || Name==TEXT("IsFalling")) && !CastField<FBoolProperty>(Property))return Fail(TEXT("Old condition is not bool"));
    if(Name==TEXT("Velocity")){const auto* Struct=CastField<FStructProperty>(Property);if(!Struct || Struct->Struct!=TBaseStructure<FVector>::Get())return Fail(TEXT("Old velocity is not FVector"));}
    R.Reads.Add(Get);++R.Counts.FindOrAdd(Name);
   }
  }
 }
 for(const auto& Pair:ExpectedNPCReads138())if(R.Counts.FindRef(Pair.Key)!=Pair.Value)return Fail(TEXT("Selected outside-EventGraph variable cardinality changed"));
 if(!ShouldWriter)return Fail(TEXT("ShouldMove writer is absent"));
 const auto* And=LinkedCall138(ShouldWriter->FindPin(TEXT("ShouldMove")));
 if(!Function138(And,TEXT("BooleanAND"),UKismetMathLibrary::StaticClass()))return Fail(TEXT("ShouldMove is no longer the observed AND expression"));
 const auto* First=LinkedCall138(And->FindPin(TEXT("A")));const auto* Second=LinkedCall138(And->FindPin(TEXT("B")));
 const UK2Node_CallFunction* Greater=nullptr;const UK2Node_CallFunction* AccelComparison=nullptr;
 for(const auto* Call:{First,Second})
 {
  if(Function138(Call,TEXT("Greater_DoubleDouble"),UKismetMathLibrary::StaticClass()) || Function138(Call,TEXT("Greater_FloatFloat"),UKismetMathLibrary::StaticClass()))Greater=Call;
  if(Function138(Call,TEXT("NotEqual_VectorVector"),UKismetMathLibrary::StaticClass()))AccelComparison=Call;
 }
 if(!Greater || !AccelComparison)return Fail(TEXT("Observed strict speed comparison and acceleration inequality were not found"));
 const auto* SpeedPin=Greater->FindPin(TEXT("A"));const auto* Literal=Greater->FindPin(TEXT("B"));
 const auto* SpeedGet=SpeedPin && SpeedPin->LinkedTo.Num()==1?Cast<UK2Node_VariableGet>(SpeedPin->LinkedTo[0]->GetOwningNode()):nullptr;
 if(!SpeedGet || SpeedGet->GetVarName()!=TEXT("GroundSpeed") || !SpeedGet->VariableReference.IsSelfContext() || !IsReal138(SpeedPin) || !IsReal138(Literal) || !Literal->LinkedTo.IsEmpty())return Fail(TEXT("Original speed threshold is not an unlinked typed literal on the expected GroundSpeed comparison"));
 R.Greater=Greater->GetTargetFunction();R.Threshold=Literal->DefaultValue;R.ThresholdType=Literal->PinType;
 if(!LexTryParseString(R.ThresholdValue,*R.Threshold) || !FMath::IsFinite(R.ThresholdValue))return Fail(TEXT("Original speed threshold is not a finite numeric literal"));
 if(!Function138(LinkedCall138(AccelComparison->FindPin(TEXT("A"))),TEXT("GetCurrentAcceleration"),UCharacterMovementComponent::StaticClass()) || !AccelComparison->FindPin(TEXT("B")) || !AccelComparison->FindPin(TEXT("B"))->LinkedTo.IsEmpty())return Fail(TEXT("Old input gate is no longer the observed CMC acceleration branch"));
 return true;
}
FString LocalObject138(const UObject* Object,const UAnimBlueprint* BP)
{
 if(!Object)return TEXT("None");
 if(Object==BP || Object==BP->GeneratedClass || Object==BP->SkeletonGeneratedClass)return TEXT("$SELF");
 if(Object->HasAnyFlags(RF_ClassDefaultObject) && (Object->GetClass()==BP->GeneratedClass || Object->GetClass()==BP->SkeletonGeneratedClass))return TEXT("$SELF_CDO");
 return Object->GetPathName();
}
bool KeptGraphStamp138(UAnimBlueprint* BP,const FNPCGraphRecipe138& R,const TSet<FString>* Restrict,TSet<FString>& Keys,FString& Stamp)
{
 TArray<FString> Lines;TSet<const UEdGraphNode*> Excluded;for(auto* Read:R.Reads)Excluded.Add(Read);
 for(auto* Graph:R.Graphs)if(Graph!=R.Event)for(const auto& Object:Graph->Nodes)
 {
  const auto* Node=Object.Get();const FString Key=Node->GetPathName(BP);
  if(Excluded.Contains(Node) || (Restrict && !Restrict->Contains(Key)))continue;
  if(Keys.Contains(Key))return false;Keys.Add(Key);Lines.Add(TEXT("N|")+Key+TEXT("|")+Node->GetClass()->GetPathName());
  if(const auto* Anim=Cast<UAnimGraphNode_Base>(Node))Lines.Add(TEXT("Asset|")+Key+TEXT("|")+GetPathNameSafe(Anim->GetAnimationAsset()));
  if(const auto* Slot=Cast<UAnimGraphNode_Slot>(Node))Lines.Add(TEXT("Slot|")+Key+TEXT("|")+Slot->Node.SlotName.ToString());
  if(const auto* Rig=Cast<UAnimGraphNode_ControlRig>(Node))Lines.Add(TEXT("Rig|")+Key+TEXT("|")+GetPathNameSafe(Rig->Node.GetControlRigAssetReference().GetRigClass()));
  for(const auto* Pin:Node->Pins)
  {
   if(!Pin)return false;const auto& T=Pin->PinType;
   Lines.Add(FString::Printf(TEXT("P|%s|%s|%d|%s|%s|%s|%d|%d|%d"),*Key,*Pin->PinName.ToString(),int32(Pin->Direction),*T.PinCategory.ToString(),*T.PinSubCategory.ToString(),*LocalObject138(T.PinSubCategoryObject.Get(),BP),int32(T.ContainerType),T.bIsReference,T.bIsConst));
   if(Pin->LinkedTo.IsEmpty())Lines.Add(TEXT("D|")+Key+TEXT("|")+Pin->PinName.ToString()+TEXT("|")+Pin->DefaultValue+TEXT("|")+Pin->DefaultTextValue.ToString()+TEXT("|")+LocalObject138(Pin->DefaultObject.Get(),BP));
   for(const auto* Other:Pin->LinkedTo)
   {
    if(!Other)return false;const auto* OtherNode=Other->GetOwningNode();const FString OtherKey=OtherNode->GetPathName(BP);
    if(OtherNode->GetGraph()!=R.Event && !Excluded.Contains(OtherNode) && (!Restrict || Restrict->Contains(OtherKey)))Lines.Add(TEXT("L|")+Key+TEXT("|")+Pin->PinName.ToString()+TEXT("|")+OtherKey+TEXT("|")+Other->PinName.ToString());
   }
  }
 }
 Lines.Sort();Stamp=FString::Join(Lines,TEXT("\n"));return !Restrict || Keys.Num()==Restrict->Num();
}
UEdGraphPin* NativeRead138(UEdGraph* Graph,FName Name,bool bFunction)
{
 if(bFunction)
 {
  auto* Function=UProject_JCharacterAnimInstance::StaticClass()->FindFunctionByName(Name);
  if(!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintPure) || !Function->HasMetaData(TEXT("BlueprintThreadSafe")))return nullptr;
  FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Node=Creator.CreateNode();Node->SetFromFunction(Function);Creator.Finalize();return Node->GetReturnValuePin();
 }
 const auto* Field=FindFProperty<FFloatProperty>(UProject_JCharacterAnimInstance::StaticClass(),Name);
 if(!Field || !Field->HasAllPropertyFlags(CPF_BlueprintVisible|CPF_BlueprintReadOnly))return nullptr;
 FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);auto* Node=Creator.CreateNode();Node->VariableReference.SetSelfMember(Name);Creator.Finalize();return Node->GetValuePin();
}
bool Connected138(const UEdGraphPin* Producer,const UEdGraphPin* Consumer)
{
 if(Consumer->LinkedTo.Contains(Producer))return true;
 // Only the ordinary real precision conversion inserted by the public schema.
 for(const auto* Link:Consumer->LinkedTo)if(const auto* Call=Cast<UK2Node_CallFunction>(Link->GetOwningNode()))
 {
  if(!Function138(Call,TEXT("Conv_FloatToDouble"),UKismetMathLibrary::StaticClass()) && !Function138(Call,TEXT("Conv_DoubleToFloat"),UKismetMathLibrary::StaticClass()))continue;
  for(const auto* Pin:Call->Pins)if(Pin->Direction==EGPD_Input && Pin->LinkedTo.Contains(Producer))return true;
 }
 return false;
}
}

FString UProject_JAssetMigrationLibrary::CreateNPCMoverUnarmed(UAnimBlueprint* Template,const FString& DestinationPackage,bool bExecute,bool bSave)
{
 const auto Result=MakeShared<FJsonObject>();Result->SetNumberField(TEXT("schema"),1);Result->SetBoolField(TEXT("execute_requested"),bExecute);Result->SetBoolField(TEXT("save_requested"),bSave);
 Result->SetStringField(TEXT("source"),GetPathNameSafe(Template));Result->SetStringField(TEXT("target_package"),DestinationPackage);Result->SetStringField(TEXT("required_parent"),UProject_JCharacterAnimInstance::StaticClass()->GetPathName());
 bool bPreflight=false,bBuilt=false,bSaved=false;FString Error;
 const auto Finish=[&]()
 {
  const bool bSuccess=Error.IsEmpty() && bPreflight && (!bExecute || bBuilt) && (!bSave || bSaved);
  Result->SetBoolField(TEXT("success"),bSuccess);Result->SetBoolField(TEXT("preflight_success"),bPreflight);Result->SetBoolField(TEXT("build_success"),bBuilt);Result->SetBoolField(TEXT("saved"),bSaved);Result->SetStringField(TEXT("reason"),Error);
  if(!bSuccess)UE_LOG(LogProjectJNPCAnimationMigration,Warning,TEXT("Selected NPC animation migration refused: %s"),*Error);
  FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
 };
 const auto Fail=[&](const TCHAR* Why){Error=Why;return Finish();};
 if(!IsInGameThread() || !Template || (bSave && !bExecute))return Fail(TEXT("Expected GT template and explicit execute before save"));
 TStrongObjectPtr<UAnimBlueprint> KeepSource(Template);const bool bSourceDirty=Template->GetOutermost()->IsDirty();
 if(bSourceDirty || !Template->GeneratedClass || Template->ParentClass!=UAnimInstance::StaticClass())return Fail(TEXT("Selected template must be clean, compiled and directly based on Engine.AnimInstance"));
 if(!DestinationPackage.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(DestinationPackage) || DestinationPackage==Template->GetOutermost()->GetName() || FPackageName::DoesPackageExist(DestinationPackage) || FindPackage(nullptr,*DestinationPackage))return Fail(TEXT("Destination must be a new valid Game package, absent on disk and in memory"));
 FNPCGraphRecipe138 SourceRecipe;if(!CollectNPCRecipe138(Template,SourceRecipe,Error))return Finish();
 TSet<FString> KeptKeys;FString SourceStamp;if(!KeptGraphStamp138(Template,SourceRecipe,nullptr,KeptKeys,SourceStamp))return Fail(TEXT("Could not capture original kept graph topology/assets/pins"));
 Result->SetStringField(TEXT("original_threshold_literal"),SourceRecipe.Threshold);Result->SetNumberField(TEXT("original_threshold_numeric"),SourceRecipe.ThresholdValue);Result->SetStringField(TEXT("strict_comparator"),SourceRecipe.Greater->GetPathName());Result->SetStringField(TEXT("should_move_policy"),TEXT("Native proxy ground speed strictly exceeds the original literal; no local input/CMC acceleration gate"));
 const auto ReadCounts=MakeShared<FJsonObject>();for(const auto& Count:ExpectedNPCReads138())ReadCounts->SetNumberField(Count.Key.ToString(),SourceRecipe.Counts.FindRef(Count.Key));Result->SetObjectField(TEXT("outside_event_read_counts"),ReadCounts);
 for(const FName Name:{GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeIsFalling),GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeVelocity)})
 {
  const auto* Function=UProject_JCharacterAnimInstance::StaticClass()->FindFunctionByName(Name);
  if(!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintPure) || !Function->HasMetaData(TEXT("BlueprintThreadSafe")))return Fail(TEXT("Required native thread-safe value getter is absent"));
  const auto* Return=Function->GetReturnProperty();
  if(Name==GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeIsFalling) && !CastField<FBoolProperty>(Return))return Fail(TEXT("Native falling getter is not bool"));
  if(Name==GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeVelocity)) {const auto* Struct=CastField<FStructProperty>(Return);if(!Struct || Struct->Struct!=TBaseStructure<FVector>::Get())return Fail(TEXT("Native velocity getter is not FVector"));}
 }
 for(const FName Name:{GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance,GraphCombatSpeed),GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance,GraphCombatDirection)})
 {const auto* Field=FindFProperty<FFloatProperty>(UProject_JCharacterAnimInstance::StaticClass(),Name);if(!Field || !Field->HasAllPropertyFlags(CPF_BlueprintVisible|CPF_BlueprintReadOnly))return Fail(TEXT("Native speed/direction property contract is absent"));}
 bPreflight=true;if(!bExecute)return Finish();
 auto& Tools=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
 TStrongObjectPtr<UAnimBlueprint> Copy(Cast<UAnimBlueprint>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(DestinationPackage),FPackageName::GetLongPackagePath(DestinationPackage),Template)));
 if(!Copy.IsValid() || Copy.Get()==Template || Copy->GetOutermost()->GetName()!=DestinationPackage)return Fail(TEXT("New target duplication failed; no explicit package save performed"));
 Result->SetStringField(TEXT("new_asset"),Copy->GetPathName());
 UBlueprintEditorLibrary::ReparentBlueprint(Copy.Get(),UProject_JCharacterAnimInstance::StaticClass()); // Supported native operation compiles with SkipSave.
 if(Copy->Status==BS_Error || Copy->ParentClass!=UProject_JCharacterAnimInstance::StaticClass())return Fail(TEXT("Reparent failed; new copy remains unsaved and no NPC binding is edited"));
 FNPCGraphRecipe138 Work;if(!CollectNPCRecipe138(Copy.Get(),Work,Error))return Finish();
 if(Work.Threshold!=SourceRecipe.Threshold || Work.ThresholdType!=SourceRecipe.ThresholdType || Work.Greater!=SourceRecipe.Greater)return Fail(TEXT("Duplication/reparent changed the original comparison/default type"));
 int32 Rewired=0,SplitLeavesRewired=0,SplitConnectionsRewired=0;
 for(auto* Read:Work.Reads)
 {
  auto* Graph=Read->GetGraph();const auto* Schema=Cast<UEdGraphSchema_K2>(Graph->GetSchema());auto* OldOutput=Read->GetValuePin();if(!Schema || !OldOutput)return Fail(TEXT("Expected a typed K2 reader graph"));
  UEdGraphPin* NewOutput=nullptr;const FName Name=Read->GetVarName();
  if(Name==TEXT("GroundSpeed"))NewOutput=NativeRead138(Graph,GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance,GraphCombatSpeed),false);
  else if(Name==TEXT("Direction"))NewOutput=NativeRead138(Graph,GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance,GraphCombatDirection),false);
  else if(Name==TEXT("IsFalling"))NewOutput=NativeRead138(Graph,GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeIsFalling),true);
  else if(Name==TEXT("Velocity"))NewOutput=NativeRead138(Graph,GET_FUNCTION_NAME_CHECKED(UProject_JCharacterAnimInstance,GetThreadSafeVelocity),true);
  else if(Name==TEXT("ShouldMove"))
  {
   auto* Speed=NativeRead138(Graph,GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance,GraphCombatSpeed),false);
   FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Compare=Creator.CreateNode();Compare->SetFromFunction(SourceRecipe.Greater);Creator.Finalize();
   auto* A=Compare->FindPin(TEXT("A"));auto* B=Compare->FindPin(TEXT("B"));NewOutput=Compare->GetReturnValuePin();
   if(!Speed || !IsReal138(A) || !B || B->PinType!=SourceRecipe.ThresholdType || !Schema->TryCreateConnection(Speed,A))return Fail(TEXT("Could not connect native speed to the original typed strict comparator"));
   Schema->TrySetDefaultValue(*B,SourceRecipe.Threshold);if(B->DefaultValue!=SourceRecipe.Threshold || !B->LinkedTo.IsEmpty())return Fail(TEXT("Original threshold literal was not preserved"));
  }
  if(!NewOutput)return Fail(TEXT("Native value node could not be created/resolved"));
  const auto MoveActualPin=[&](UEdGraphPin* OldPin,UEdGraphPin* NewPin)
  {
   const auto Consumers=OldPin->LinkedTo;const auto Response=Schema->MovePinLinks(*OldPin,*NewPin,false,true);
   if(Response.Response==CONNECT_RESPONSE_DISALLOW)return false;
   for(const auto* Consumer:Consumers)if(!Connected138(NewPin,Consumer))return false;
   return OldPin->LinkedTo.IsEmpty();
  };
  if(!OldOutput->SubPins.IsEmpty())
  {
   TArray<UEdGraphPin*> OldLeaves,NewLeaves;FString Shape;
   if(Name!=TEXT("Velocity") || !VelocityLeaves138(OldOutput,OldLeaves,true,Shape) || !Schema->CanSplitStructPin(*NewOutput))return Fail(TEXT("Expected the actual Z-only FVector split reader and a splittable native return"));
   Schema->SplitPin(NewOutput,true);
   if(!VelocityLeaves138(NewOutput,NewLeaves,false,Shape))return Fail(TEXT("Public schema did not create the matching native FVector XYZ output leaves"));
   for(int32 Index=0;Index<3;++Index)
   {
    auto* OldLeaf=OldLeaves[Index];auto* NewLeaf=NewLeaves[Index];
    if(OldLeaf->PinType!=NewLeaf->PinType)return Fail(TEXT("Native FVector split leaf type differs from the original"));
    if(NewLeaf->DefaultValue!=OldLeaf->DefaultValue)Schema->TrySetDefaultValue(*NewLeaf,OldLeaf->DefaultValue);
    if(NewLeaf->DefaultValue!=OldLeaf->DefaultValue || NewLeaf->DefaultObject || !NewLeaf->DefaultTextValue.IsEmpty())return Fail(TEXT("Native split leaf default was not preserved through the public schema"));
    const int32 Connections=OldLeaf->LinkedTo.Num();
    if(Connections && !MoveActualPin(OldLeaf,NewLeaf))return Fail(TEXT("An actual FVector leaf consumer did not survive schema move/type readback"));
    if(!OldLeaf->LinkedTo.IsEmpty() || (Connections==0 && !NewLeaf->LinkedTo.IsEmpty()))return Fail(TEXT("Split reader retained an old edge or gained an unused leaf edge"));
    ++SplitLeavesRewired;SplitConnectionsRewired+=Connections;
   }
   if(!OldOutput->LinkedTo.IsEmpty())return Fail(TEXT("Retired FVector parent unexpectedly owns a connection"));
  }
  else if(!MoveActualPin(OldOutput,NewOutput))return Fail(TEXT("A scalar reader consumer did not survive schema connection/conversion"));
  FBlueprintEditorUtils::RemoveNode(Copy.Get(),Read,true);++Rewired;
 }
 // Only this copy's old owner/CMC EventGraph is removed, never a shared source.
 for(UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Work.Event->Nodes))FBlueprintEditorUtils::RemoveNode(Copy.Get(),Node,true);
 if(!Work.Event->Nodes.IsEmpty())return Fail(TEXT("Old EventGraph writers were not fully retired"));
 // Revalidate the generated closure after the final compile below; no old
 // actor/CMC writer or cached read is allowed to survive this copy.
 for(auto* Graph:Work.Graphs)for(UEdGraphNode* Node:Graph->Nodes)if(const auto* Variable=Cast<UK2Node_Variable>(Node))if(ExpectedNPCReads138().Contains(Variable->GetVarName()))return Fail(TEXT("An old variable reference remains before variable removal"));
 for(const auto& Count:ExpectedNPCReads138())FBlueprintEditorUtils::RemoveMemberVariable(Copy.Get(),Count.Key);
 for(const auto& Variable:Copy->NewVariables)if(ExpectedNPCReads138().Contains(Variable.VarName))return Fail(TEXT("A retired template member remains"));
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Copy.Get());FCompilerResultsLog CompilerLog;
 FKismetEditorUtilities::CompileBlueprint(Copy.Get(),EBlueprintCompileOptions::SkipSave,&CompilerLog);
 if(CompilerLog.NumErrors || Copy->Status==BS_Error || !Copy->GeneratedClass || !Copy->GeneratedClass->IsChildOf(UProject_JCharacterAnimInstance::StaticClass()) || Copy->TargetSkeleton!=Template->TargetSkeleton)return Fail(TEXT("Final clone compile/parent/skeleton validation failed; no save"));
 const auto* FinalInterface=IAnimClassInterface::GetFromClass(Copy->GeneratedClass);
 if(!FinalInterface || FinalInterface->GetAnimNodeProperties().IsEmpty() || FinalInterface->GetAnimNodeProperties().Num()>4096)return Fail(TEXT("Final compiled graph is missing or outside bounded scope"));
 for(const auto* Property:FinalInterface->GetAnimNodeProperties())if(!Property || !Property->Struct || Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()) || Property->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct()) || Property->Struct->IsChildOf(FAnimNode_LinkedAnimGraph::StaticStruct()))return Fail(TEXT("Final graph unexpectedly contains PoseSearch or linked animation execution"));
 FNPCGraphRecipe138 KeptWork;Copy->GetAllGraphs(KeptWork.Graphs);KeptWork.Event=Work.Event;TSet<FString> NewKeys;FString NewStamp;
 if(!KeptGraphStamp138(Copy.Get(),KeptWork,&KeptKeys,NewKeys,NewStamp) || NewStamp!=SourceStamp)return Fail(TEXT("A retained pose graph/asset/slot/ControlRig/pin or non-reader link changed; no save"));
 FNPCGraphRecipe138 SourceAgain;FString SourceAfter;TSet<FString> SourceKeys;
 if(!CollectNPCRecipe138(Template,SourceAgain,Error) || !KeptGraphStamp138(Template,SourceAgain,nullptr,SourceKeys,SourceAfter) || SourceAfter!=SourceStamp || Template->GetOutermost()->IsDirty()!=bSourceDirty)return Fail(TEXT("Original template metadata/dirty state changed; no save"));
 Result->SetBoolField(TEXT("original_kept_graph_and_dirty_unchanged"),true);Result->SetBoolField(TEXT("new_kept_pose_graph_equivalent"),true);Result->SetNumberField(TEXT("rewired_read_nodes"),Rewired);Result->SetNumberField(TEXT("rewired_vector_split_leaves"),SplitLeavesRewired);Result->SetNumberField(TEXT("rewired_vector_split_connections"),SplitConnectionsRewired);Result->SetNumberField(TEXT("compile_errors"),CompilerLog.NumErrors);Result->SetNumberField(TEXT("compile_warnings"),CompilerLog.NumWarnings);bBuilt=true;
 if(bSave)
 {
  if(FPackageName::DoesPackageExist(DestinationPackage))return Fail(TEXT("Destination appeared before save; overwriting is forbidden"));
  const FString File=FPackageName::LongPackageNameToFilename(DestinationPackage,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
  FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
  bSaved=UPackage::SavePackage(Copy->GetOutermost(),Copy.Get(),*File,Args);if(!bSaved)return Fail(TEXT("Explicit new-package save failed; inspect partial output, no original/NPC package was saved"));
 }
 return Finish();
}
