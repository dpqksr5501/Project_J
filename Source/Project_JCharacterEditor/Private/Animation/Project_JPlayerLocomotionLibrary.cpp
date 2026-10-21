#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"
#include "Animation/AnimBlueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Retargeter/IKRetargeter.h"
#include "ChooserPropertyAccess.h"
#include "EdGraphUtilities.h"
#include "K2Node_VariableGet.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JPlayerAirTypes.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "EnumColumn.h"
#include "ChooserTypes.h"
#include "ObjectChooser_Asset.h"
#include "AnimGraphNode_BlendStack.h"
#include "AnimStateTransitionNode.h"
#include "Movement/Project_JMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"

bool UProject_JPlayerLocomotionLibrary::UpdateRetargetedAnimationData(UAnimSequence* Target, UAnimSequence* Converted)
{
	if (!IsValid(Target) || !IsValid(Converted) || Target == Converted ||
		Target->GetSkeleton() != Converted->GetSkeleton() ||
		!Target->IsDataModelValid() || !Converted->IsDataModelValid()) return false;
	const IAnimationDataModel* Before = Target->GetDataModel();
	const IAnimationDataModel* After = Converted->GetDataModel();
	// A different timeline could invalidate the target's notify and sync-marker timing.
	if (Before->GetFrameRate() != After->GetFrameRate() ||
		Before->GetNumberOfFrames() != After->GetNumberOfFrames()) return false;
	TArray<FName> TargetNames, ConvertedNames;
	Before->GetBoneTrackNames(TargetNames);
	After->GetBoneTrackNames(ConvertedNames);
	if (TargetNames.Num() != ConvertedNames.Num()) return false;
	TMap<FName, TArray<FTransform>> Tracks;
	for (const FName Name : ConvertedNames)
	{
		if (!TargetNames.Contains(Name)) return false;
		TArray<FTransform>& Keys = Tracks.Add(Name);
		After->GetBoneTrackTransforms(Name, Keys);
		if (Keys.Num() != After->GetNumberOfKeys()) return false;
		for (const FTransform& Key : Keys) if (Key.ContainsNaN()) return false;
	}
	Target->Modify();
	IAnimationDataController& Controller = Target->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller,
		NSLOCTEXT("ProjectJLocomotion", "UpdateRetargetedTracks", "Update retargeted bone tracks"), false);
	for (const auto& Track : Tracks)
	{
		TArray<FVector> Positions, Scales;
		TArray<FQuat> Rotations;
		Positions.Reserve(Track.Value.Num());
		Scales.Reserve(Track.Value.Num());
		Rotations.Reserve(Track.Value.Num());
		for (const FTransform& Key : Track.Value)
		{
			Positions.Add(Key.GetTranslation());
			Rotations.Add(Key.GetRotation());
			Scales.Add(Key.GetScale3D());
		}
		if (!Controller.SetBoneTrackKeys(Track.Key, Positions, Rotations, Scales, false)) return false;
	}
	Target->MarkPackageDirty();
	return true;
}

namespace
{
UEdGraph* FindFoundationFunctionGraph(UBlueprint* Blueprint, FName Name)
{
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == Name) return Graph;
	return nullptr;
}

bool ReplaceVoidFunction(UBlueprint* Blueprint, FName Name, UFunction* Replacement)
{
	auto* Graph = FindFoundationFunctionGraph(Blueprint, Name);
	if (!Graph) return false;
	UK2Node_FunctionEntry* Entry = nullptr;
	UK2Node_FunctionResult* Result = nullptr;
	const auto Nodes = Graph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (auto* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node)) Entry = FunctionEntry;
		else if (auto* FunctionResult = Cast<UK2Node_FunctionResult>(Node)) Result = FunctionResult;
		else FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	}
	if (!Entry) return false;
	for (auto* Pin : Entry->Pins) Pin->BreakAllPinLinks();
	if (Result) for (auto* Pin : Result->Pins) Pin->BreakAllPinLinks();
	UEdGraphPin* Then = Entry->GetThenPin();
	if (Replacement)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		auto* Call = Creator.CreateNode();
		Call->SetFromFunction(Replacement);
		Creator.Finalize();
		if (!Graph->GetSchema()->TryCreateConnection(Then, Call->GetExecPin())) return false;
		Then = Call->GetThenPin();
	}
	if (Result) return Graph->GetSchema()->TryCreateConnection(Then, Result->FindPin(UEdGraphSchema_K2::PN_Execute));
	return true;
}

void KeepExecution(UEdGraphNode* Node, TSet<UEdGraphNode*>& Keep)
{
	if (!Node || Keep.Contains(Node)) return;
	Keep.Add(Node);
	for (auto* Pin : Node->Pins)
	{
		if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
		for (auto* Linked : Pin->LinkedTo) KeepExecution(Linked->GetOwningNode(), Keep);
	}
}

void KeepInputDependencies(UEdGraphNode* Node, TSet<UEdGraphNode*>& Keep)
{
	for (auto* Pin : Node->Pins)
	{
		if (Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		for (auto* Linked : Pin->LinkedTo)
		{
			auto* Provider = Linked->GetOwningNode();
			if (Keep.Contains(Provider)) continue;
			Keep.Add(Provider);
			KeepInputDependencies(Provider, Keep);
		}
	}
}

void ReplaceSampleLifecycle(UBlueprint* Blueprint)
{
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		TSet<UEdGraphNode*> Keep;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node_CustomEvent>(Node)) KeepExecution(Node, Keep);
			else if (const auto* Event = Cast<UK2Node_Event>(Node))
			{
				const FName Name = Event->EventReference.GetMemberName();
				if (Name != TEXT("BlueprintInitializeAnimation") && Name != TEXT("BlueprintBeginPlay") &&
					Name != TEXT("BlueprintUpdateAnimation")) KeepExecution(Node, Keep);
			}
		}
		const auto Executable = Keep.Array();
		for (auto* Node : Executable) KeepInputDependencies(Node, Keep);
		const auto Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes) if (!Keep.Contains(Node)) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	}
}
}

bool UProject_JPlayerLocomotionLibrary::InstallNativeTransitionNotify(UBlueprint* Notify)
{
 if (!Notify || !Notify->GeneratedClass || !Notify->GeneratedClass->IsChildOf(UAnimNotifyState::StaticClass())) return false;
 auto* Graph=FindFoundationFunctionGraph(Notify,TEXT("Received_NotifyTick"));
 if (!Graph) return false;
 UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;
 for (UEdGraphNode* Node:Graph->Nodes)
 {
  if (auto* Candidate=Cast<UK2Node_FunctionEntry>(Node)) Entry=Candidate;
  if (!Result) Result=Cast<UK2Node_FunctionResult>(Node);
 }
 if (!Entry || !Entry->FindPin(TEXT("MeshComp")) || !Entry->FindPin(TEXT("EventReference"))) return false;
 const FName Fields[]={TEXT("TransitionDestination"),TEXT("TransitionCondition"),TEXT("GaitNotEqual")};
 for (FName Field:Fields)
 {
  const auto* Property=FindFProperty<FByteProperty>(Notify->GeneratedClass,Field);
  if (!Property || !Property->Enum || !Property->Enum->IsValidEnumValue(0) || !Property->Enum->IsValidEnumValue(1)) return false;
 }
 Notify->Modify();Graph->Modify();
 for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
 {
  Node->BreakAllNodeLinks();
  if (Node!=Entry && Node!=Result) FBlueprintEditorUtils::RemoveNode(Notify,Node,true);
 }
 FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Call=Creator.CreateNode();
 Call->SetFromFunction(UProject_JAnimNotifyState_LocomotionEarlyTransition::StaticClass()->FindFunctionByName(
  GET_FUNCTION_NAME_CHECKED(UProject_JAnimNotifyState_LocomotionEarlyTransition,RequestGASPTransition)));Creator.Finalize();
 const auto* Schema=Graph->GetSchema();
 if (!Schema->TryCreateConnection(Entry->GetThenPin(),Call->GetExecPin())) return false;
 for (FName Pin:{FName(TEXT("MeshComp")),FName(TEXT("EventReference"))})
  if (!Schema->TryCreateConnection(Entry->FindPin(Pin),Call->FindPin(Pin))) return false;
 const FName Inputs[]={TEXT("Destination"),TEXT("Condition"),TEXT("GaitNotEqual")};
 for (int32 Index=0;Index<3;++Index)
 {
  FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Graph);auto* Get=GetCreator.CreateNode();
  Get->VariableReference.SetSelfMember(Fields[Index]);GetCreator.Finalize();
  if (!Schema->TryCreateConnection(Get->GetValuePin(),Call->FindPin(Inputs[Index]))) return false;
 }
 if (Result && !Schema->TryCreateConnection(Call->GetThenPin(),Result->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Notify);FKismetEditorUtilities::CompileBlueprint(Notify);
 Notify->MarkPackageDirty();return Notify->Status!=BS_Error;
}

UAnimBlueprint* UProject_JPlayerLocomotionLibrary::PrepareFoundation(UAnimBlueprint* Source, const FString& DestinationPackage)
{
	if (!Source || !FPackageName::IsValidLongPackageName(DestinationPackage) || FPackageName::DoesPackageExist(DestinationPackage)) return nullptr;
	auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	auto* Blueprint = Cast<UAnimBlueprint>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(DestinationPackage),
		FPackageName::GetLongPackagePath(DestinationPackage), Source));
	if (!Blueprint) return nullptr;
	Blueprint->ParentClass = UProject_JPlayerAnimInstance::StaticClass();
	ReplaceSampleLifecycle(Blueprint);
	if (!ReplaceVoidFunction(Blueprint, TEXT("Update_PropertiesFromCharacter"), nullptr))
	{
		UE_LOG(LogTemp, Error, TEXT("Actual GASP source contract changed; refusing incomplete backend replacement"));
		return nullptr;
	}
	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, TEXT("HasMover"), TEXT("HasLocomotionSource"));
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, TEXT("Mover"));
	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, TEXT("Predictor"));
	ReplaceVoidFunction(Blueprint, TEXT("DebugDraws"), nullptr);
	if (!RestoreFoundationTrajectory(Source,Blueprint)) return nullptr;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error) return nullptr;
	if (auto* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr)
	{
		if (auto* Setup = FindFProperty<FIntProperty>(Defaults->GetClass(), TEXT("LocomotionSetup"))) Setup->SetPropertyValue_InContainer(Defaults, 1);
		if (auto* MMI = FindFProperty<FBoolProperty>(Defaults->GetClass(), TEXT("Enable_MMI_IK"))) MMI->SetPropertyValue_InContainer(Defaults, false);
	}
	Blueprint->MarkPackageDirty();
	return Blueprint;
}

int32 UProject_JPlayerLocomotionLibrary::RebindFoundationChoosers(UAnimBlueprint* Source, UAnimBlueprint* Target)
{
	if (!Source || !Target || !Source->GeneratedClass || !Target->GeneratedClass || Target->Status == BS_Error) return INDEX_NONE;
	ReplaceVoidFunction(Target, TEXT("DebugDraws"), nullptr);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
	FKismetEditorUtilities::CompileBlueprint(Target);
	if (Target->Status == BS_Error) return INDEX_NONE;
	Target->MarkPackageDirty();
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game/GASP58Native"));
	Filter.ClassPaths.Add(UChooserTable::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
	int32 Changed = 0;
	for (const FAssetData& Asset : Assets)
	{
		auto* Table = Cast<UChooserTable>(Asset.GetAsset());
		if (!Table) continue;
		bool bChanged = false;
		for (auto& Parameter : Table->ContextData)
		{
			auto* Context = Parameter.GetMutablePtr<FContextObjectTypeClass>();
			if (!Context || (Context->Class != Source->GeneratedClass && Context->Class != Target->GeneratedClass)) continue;
			Table->Modify();
			Context->Class = Target->GeneratedClass;
			bChanged = true;
		}
		if (bChanged)
		{
			Table->Compile(true);
			TArray<UObject*> NestedObjects;
			GetObjectsWithOuter(Table, NestedObjects, EGetObjectsFlags::IncludeNestedObjects);
			for (UObject* Object : NestedObjects)
				if (auto* NestedTable = Cast<UChooserTable>(Object)) NestedTable->Compile(true);
			Table->MarkPackageDirty();
			++Changed;
		}
	}
	return Changed;
}

bool UProject_JPlayerLocomotionLibrary::RestoreFoundationTrajectory(UAnimBlueprint* Source, UAnimBlueprint* Target)
{
	if (!Source || !Target) return false;
	auto* Original = FindFoundationFunctionGraph(Source, TEXT("Update_Trajectory"));
	auto* Existing = FindFoundationFunctionGraph(Target, TEXT("Update_Trajectory"));
	if (!Original || !Existing) return false;
	FBlueprintEditorUtils::RemoveGraph(Target, Existing, EGraphRemoveFlags::None);
	Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	auto* Graph = FEdGraphUtilities::CloneGraph(Original, Target);
	if (!Graph) return false;
	if (!Graph->Rename(TEXT("Update_Trajectory"), Target, REN_DontCreateRedirectors | REN_NonTransactional)) return false;
	Target->FunctionGraphs.Add(Graph);
	UK2Node_CallFunction* Producer = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
		if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Call->FunctionReference.GetMemberName() == TEXT("PoseSearchGenerateTransformTrajectoryWithPredictor")) Producer = Call;
	if (!Producer) return false;
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
	auto* ReadSnapshot = Creator.CreateNode();
	ReadSnapshot->SetFromFunction(UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance, GetFoundationTrajectory)));
	Creator.Finalize();
	auto* Return = ReadSnapshot->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	auto* Trajectory = Producer->FindPin(TEXT("OutTrajectory"));
	if (!Return || !Trajectory || !Producer->GetExecPin() || !Producer->GetThenPin()) return false;
	const auto Consumers = Trajectory->LinkedTo;
	const auto Before = Producer->GetExecPin()->LinkedTo;
	const auto After = Producer->GetThenPin()->LinkedTo;
	Trajectory->BreakAllPinLinks();
	for (auto* Pin : Consumers) if (!Graph->GetSchema()->TryCreateConnection(Return, Pin)) return false;
	Producer->GetExecPin()->BreakAllPinLinks();
	Producer->GetThenPin()->BreakAllPinLinks();
	for (auto* From : Before) for (auto* To : After) if (!Graph->GetSchema()->TryCreateConnection(From, To)) return false;
	FBlueprintEditorUtils::RemoveNode(Target, Producer, true);
	const auto Nodes = Graph->Nodes;
	for (UEdGraphNode* Node : Nodes)
		if (auto* Get = Cast<UK2Node_VariableGet>(Node); Get && Get->GetVarName() == TEXT("Predictor")) FBlueprintEditorUtils::RemoveNode(Target, Get, true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
	FBlueprintEditorUtils::RefreshAllNodes(Target);
	FKismetEditorUtilities::CompileBlueprint(Target);
	Target->MarkPackageDirty();
	return Target->Status != BS_Error;
}


bool UProject_JPlayerLocomotionLibrary::RepairFullBodyMontageOrder(UAnimBlueprint* Target)
{
 auto* Graph = Target ? FindFoundationFunctionGraph(Target, TEXT("AnimGraph")) : nullptr;
 if (!Graph) return false;
 UAnimGraphNode_Slot* Slot = nullptr;
 UAnimGraphNode_LinkedAnimLayer* FullBody = nullptr;
 UAnimGraphNode_SaveCachedPose* Ground = nullptr;
 for (UEdGraphNode* Node : Graph->Nodes)
 {
  if (auto* Candidate = Cast<UAnimGraphNode_Slot>(Node); Candidate && Candidate->Node.SlotName == TEXT("DefaultSlot"))
  {
   // A duplicate playback owner is an authoring error, not something to silently remove.
   if (Slot) return false;
   Slot = Candidate;
  }
  if (auto* Layer = Cast<UAnimGraphNode_LinkedAnimLayer>(Node); Layer && Layer->Node.Layer == TEXT("Procedural_FullBody")) FullBody = Layer;
  if (auto* Cache = Cast<UAnimGraphNode_SaveCachedPose>(Node); Cache && Cache->CacheName == TEXT("ProjectJ_FinalGroundPose")) Ground = Cache;
 }
 if (!Slot || !FullBody || !Ground) return false;
 auto* Input = Slot->FindPin(TEXT("Source"));
 auto* Output = Slot->FindPin(TEXT("Pose"));
 auto* ProceduralOutput = FullBody->FindPin(TEXT("Pose"));
 auto* GroundInput = Ground->FindPin(TEXT("Pose"));
 if (!Input || !Output || !ProceduralOutput || !GroundInput) return false;
 const bool bOrdered = Input->LinkedTo.Num() == 1 && Input->LinkedTo[0] == ProceduralOutput &&
     Output->LinkedTo.Num() == 1 && Output->LinkedTo[0] == GroundInput;
 if (bOrdered && Slot->Node.bAlwaysUpdateSourcePose) return Target->Status != BS_Error;
 if (!bOrdered && (Input->LinkedTo.Num() != 1 || Output->LinkedTo.Num() != 1 ||
     GroundInput->LinkedTo.Num() != 1 || GroundInput->LinkedTo[0] != ProceduralOutput)) return false;
 Target->Modify(); Slot->Modify(); Ground->Modify();
 if (!bOrdered)
 {
  auto* Before = Input->LinkedTo[0]; auto* After = Output->LinkedTo[0];
  Input->BreakAllPinLinks(); Output->BreakAllPinLinks(); GroundInput->BreakAllPinLinks();
  const auto* Schema = Graph->GetSchema();
  if (!Schema->TryCreateConnection(Before, After) ||
      !Schema->TryCreateConnection(ProceduralOutput, Input) ||
      !Schema->TryCreateConnection(Output, GroundInput)) return false;
 }
 // Continue GASP state/stack clocks while a full-weight skill masks evaluation.
 // Cancellation and blend-out must resume the live locomotion state, not its old time.
 Slot->Node.bAlwaysUpdateSourcePose = true;
 Slot->NodePosX = Ground->NodePosX - 250;
 Slot->NodePosY = Ground->NodePosY;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
 FKismetEditorUtilities::CompileBlueprint(Target);
 Target->MarkPackageDirty();
 return Target->Status != BS_Error;
}

TArray<FString> UProject_JPlayerLocomotionLibrary::InspectGameplaySockets(USkeletalMesh* Previous, USkeletalMesh* Target)
{
 TArray<FString> Result;
 if (!Previous || !Target) return Result;
 const auto& Before=Previous->GetRefSkeleton(); const auto& After=Target->GetRefSkeleton();
 for (const auto* Socket : Previous->GetActiveSocketList())
 {
  if (!Socket) continue;
  const int32 A=Before.FindBoneIndex(Socket->BoneName), B=After.FindBoneIndex(Socket->BoneName);
  if (A==INDEX_NONE || B==INDEX_NONE) {Result.Add(FString::Printf(TEXT("INVALID %s Bone=%s"),*Socket->SocketName.ToString(),*Socket->BoneName.ToString()));continue;}
  const auto& Old=Before.GetRefBonePose()[A]; const auto& New=After.GetRefBonePose()[B];
  Result.Add(FString::Printf(TEXT("Socket=%s Bone=%s Exists=%d LocalRefAngle=%.4f LocalRefTranslation=%.4f Transform=%s"),
   *Socket->SocketName.ToString(),*Socket->BoneName.ToString(),Target->FindSocket(Socket->SocketName)!=nullptr,
   FMath::RadiansToDegrees(Old.GetRotation().AngularDistance(New.GetRotation())),FVector::Distance(Old.GetTranslation(),New.GetTranslation()),*Socket->GetSocketLocalTransform().ToString()));
 }
 return Result;
}

bool UProject_JPlayerLocomotionLibrary::MigrateGameplaySockets(USkeletalMesh* Previous, USkeletalMesh* Target, const TArray<FName>& RequiredSockets)
{
 if (!Previous || !Target || Previous==Target) return false;
 TArray<const USkeletalMeshSocket*> Sockets;
 for (FName Name : RequiredSockets)
 {
  const auto* Socket=Previous->FindSocket(Name);
  if (!Socket) return false;
  Sockets.AddUnique(Socket);
 }
 // Validate the whole requested set before mutating. Mesh sockets override sample
 // skeleton sockets without changing the shared imported skeleton or other users.
 for (const auto* Socket : Sockets)
 {
  if (!Socket) continue;
  if (Target->GetRefSkeleton().FindBoneIndex(Socket->BoneName)==INDEX_NONE) return false;
  if (const auto* Existing=Target->FindSocket(Socket->SocketName))
   if (Existing->BoneName!=Socket->BoneName || !Existing->GetSocketLocalTransform().Equals(Socket->GetSocketLocalTransform(),.001f)) return false;
 }
 Target->Modify();
 for (const auto* Socket : Sockets)
 {
  if (!Socket) continue;
  if (Target->FindSocket(Socket->SocketName)) continue;
  auto* Copy=DuplicateObject<USkeletalMeshSocket>(Socket,Target);
  if (!Copy) return false;
  Target->GetMeshOnlySocketList().Add(Copy);
 }
 Target->MarkPackageDirty();
 return true;
}

bool UProject_JPlayerLocomotionLibrary::ConfigureGameplayAvatar(UBlueprint* Avatar, USkeletalMesh* SourceMesh, UAnimBlueprint* Foundation)
{
 if (!Avatar || !SourceMesh || !Foundation || !Foundation->GeneratedClass || Foundation->Status == BS_Error) return false;
 auto* Defaults = Avatar->GeneratedClass ? Cast<AProject_JMoverPawn>(Avatar->GeneratedClass->GetDefaultObject()) : nullptr;
 if (!Defaults) return false;
 auto* Mover=Cast<UProject_JMoverComponent>(Defaults->GetMover()); if (!Mover) return false;
 UClass* WalkingClass=Mover->GroundWalkingMode.LoadSynchronous();
 if(!WalkingClass) return false;
 auto* CurrentWalking=Mover->MovementModes.FindRef(DefaultModeNames::Walking).Get();
 if(!CurrentWalking || CurrentWalking->GetClass()!=WalkingClass)
  Mover->MovementModes.Add(DefaultModeNames::Walking,NewObject<UBaseMovementMode>(Mover,WalkingClass,NAME_None,RF_Transactional|RF_ArchetypeObject));
 if(!Cast<UProject_JFallingMode>(Mover->MovementModes.FindRef(DefaultModeNames::Falling)))
  Mover->MovementModes.Add(DefaultModeNames::Falling,NewObject<UProject_JFallingMode>(Mover,NAME_None,RF_Transactional|RF_ArchetypeObject));
 FPropertyChangedEvent ModesChanged(FindFProperty<FProperty>(UMoverComponent::StaticClass(),GET_MEMBER_NAME_CHECKED(UMoverComponent,MovementModes)));
 static_cast<UObject*>(Mover)->PostEditChangeProperty(ModesChanged);
 if (auto* Property = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("MotionMatchingTrajectoryComponent")))
 {
  if (auto* Retired = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(Defaults)))
  {
   Defaults->RemoveOwnedComponent(Retired);
   Property->SetObjectPropertyValue_InContainer(Defaults, nullptr);
   Retired->DestroyComponent();
  }
 }
 Defaults->GetMesh()->SetSkeletalMeshAsset(SourceMesh);
 Defaults->GetMesh()->SetAnimInstanceClass(Foundation->GeneratedClass);
 Defaults->GetMesh()->ComponentTags.AddUnique(TEXT("ProjectJ.PoseSource"));
 Avatar->MarkPackageDirty();
 return true;
}


UChooserTable* UProject_JPlayerLocomotionLibrary::DuplicateProjectAirChooser(UChooserTable* Template, const FString& DestinationPackage)
{
 if (!Template || Template->ContextData.Num() != 3 ||
     !FPackageName::IsValidLongPackageName(DestinationPackage) || FPackageName::DoesPackageExist(DestinationPackage)) return nullptr;
 const UScriptStruct* Expected[] = {FProject_JPlayerAirQuery::StaticStruct(), FChooserPlayerSettings::StaticStruct(), FProject_JStateControllerChooserOutput::StaticStruct()};
 for (int32 Index = 0; Index < 3; ++Index)
 {
  const auto* Context = Template->ContextData[Index].GetPtr<FContextObjectTypeStruct>();
  if (!Context || Context->Struct != Expected[Index]) return nullptr;
 }
 auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
 // Duplicate the self-contained typed tree, including row output metadata and nested ownership.
 // No obsolete AnimInstance-property chooser or binding-name conversion is involved.
 auto* Copy = Cast<UChooserTable>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(DestinationPackage),
     FPackageName::GetLongPackagePath(DestinationPackage), Template));
 if (!Copy) return nullptr;
 Copy->Compile(true);
 for (UChooserTable* Nested : Copy->NestedChoosers) if (Nested) Nested->Compile(true);
 Copy->MarkPackageDirty();
 return Copy;
}

bool UProject_JPlayerLocomotionLibrary::RestoreFoundationFallOff(UAnimBlueprint* Target)
{
 if(!Target || !Target->GeneratedClass) return false;
 const auto* Defaults=Cast<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject());
 if(!Defaults)return false;
 for(FName Name:{FName(TEXT("OnStateEntry_InAirTransition")),FName(TEXT("OnStateEntry_InAirLoop"))})
 {
  const bool bLoop=Name==TEXT("OnStateEntry_InAirLoop");
  const FName Function=bLoop?GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirLoop):GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirTransition);
  if(!ReplaceVoidFunction(Target,Name,UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(Function))) return false;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FKismetEditorUtilities::CompileBlueprint(Target);
 Target->MarkPackageDirty();return Target->Status!=BS_Error;
}

bool UProject_JPlayerLocomotionLibrary::InstallProjectAir(UAnimBlueprint* Target,UChooserTable* Chooser)
{
 if (!Target || !Chooser) return false;
 auto* Function=UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,BeginProjectAir));
 for (FName Name : {FName(TEXT("OnStateEntry_InAirTransition")),FName(TEXT("OnStateEntry_InAirLoop"))})
 {
  if (!ReplaceVoidFunction(Target,Name,Function)) return false;
  auto* Graph=FindFoundationFunctionGraph(Target,Name);
  for (UEdGraphNode* Node : Graph->Nodes) if (auto* Call=Cast<UK2Node_CallFunction>(Node))
   if (Call->FunctionReference.GetMemberName()==Function->GetFName())
    if (auto* Loop=Call->FindPin(TEXT("bLoop"))) Loop->DefaultValue=Name==TEXT("OnStateEntry_InAirLoop")?TEXT("true"):TEXT("false");
 }
 auto* Graph=FindFoundationFunctionGraph(Target,TEXT("AnimGraph")); if (!Graph) return false;
 bool bFound=false;
 for (UEdGraphNode* Node : Graph->Nodes) if (auto* Stack=Cast<UAnimGraphNode_BlendStack>(Node); Stack && Stack->GetFName()==TEXT("AnimGraphNode_BlendStack_3"))
 { Stack->UpdateFunction.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateProjectAir),UProject_JPlayerAnimInstance::StaticClass()); bFound=true; }
 if (!bFound) return false;
 // The original rule reads the stack's previous ground asset on the entry frame.
 // Qualify completion against this episode's actual selected air player instead.
 TArray<UEdGraph*> AllGraphs; Target->GetAllGraphs(AllGraphs);
 for (UEdGraph* Rule : AllGraphs) for (UEdGraphNode* Node : Rule->Nodes)
  if (auto* Call=Cast<UK2Node_CallFunction>(Node); Call && (Call->FunctionReference.GetMemberName()==TEXT("IsAnimationAlmostComplete") || Call->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,IsProjectAirTransitionComplete)))
  {
   const auto* Transition=Cast<UAnimStateTransitionNode>(Rule->GetOuter());
   const bool bAirRule=Transition && Transition->GetPreviousState() && Transition->GetPreviousState()->GetStateName().Contains(TEXT("In Air"));
   if (bAirRule) Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,IsProjectAirTransitionComplete),UProject_JPlayerAnimInstance::StaticClass());
   else Call->FunctionReference.SetSelfMember(TEXT("IsAnimationAlmostComplete"));
   Call->ReconstructNode();
  }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FKismetEditorUtilities::CompileBlueprint(Target);
 if (Target->Status==BS_Error) return false;
 auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject());
 Defaults->AirAnimationChooser=Chooser;
 const auto* StateProperty=FindFProperty<FEnumProperty>(Target->GeneratedClass,GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerStateMachineState));
 if(!StateProperty || StateProperty->GetEnum()!=StaticEnum<EProject_JPlayerPlaybackState>())return false;
 Target->MarkPackageDirty(); return RestoreFoundationFallOff(Target);
}

TArray<FString> UProject_JPlayerLocomotionLibrary::InspectProjectAirCoverage(UChooserTable* Chooser)
{
 TArray<FString> Result;
 for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement,EProject_JLocomotionRotationMode::Strafe})
 for (auto Direction : {EProject_JStateControllerStrafeDirection::Forward,EProject_JStateControllerStrafeDirection::ForwardLeft,EProject_JStateControllerStrafeDirection::Left,EProject_JStateControllerStrafeDirection::BackwardLeft,EProject_JStateControllerStrafeDirection::Backward,EProject_JStateControllerStrafeDirection::BackwardRight,EProject_JStateControllerStrafeDirection::Right,EProject_JStateControllerStrafeDirection::ForwardRight})
 {
  FProject_JPlayerAirQuery Query; Query.State=EProject_JStateControllerPresentationState::TransitionToInAir; Query.RotationMode=Mode; Query.Gait=EProject_JLocomotionGaitIntent::Run; Query.Foot=EProject_JStateControllerFoot::Left; Query.Direction=Direction;
  FChooserPlayerSettings Settings; FProject_JStateControllerChooserOutput Output;
  FChooserEvaluationContext Context; Context.AddStructParam(Query);Context.AddStructParam(Settings);Context.AddStructParam(Output);
  UObject* Asset=nullptr;
  UChooserTable::EvaluateChooser(Context,Chooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Candidate){Asset=Candidate;return FObjectChooserBase::EIteratorStatus::Stop;}));
  Result.Add(FString::Printf(TEXT("Mode=%d Direction=%d Asset=%s Start=%.3f Blend=%.3f"),int32(Mode),int32(Direction),*GetPathNameSafe(Asset),Output.StartTime,Output.BlendTime));
 }
 return Result;
}

int32 UProject_JPlayerLocomotionLibrary::ConfigureStandingLocomotion(UChooserTable* Root)
{
 if (!Root || !Root->GetPathName().StartsWith(TEXT("/Game/GASP58Native/"))) return INDEX_NONE;
 TArray<UChooserTable*> Tables{Root};
 TArray<UObject*> Nested; GetObjectsWithOuter(Root,Nested,EGetObjectsFlags::IncludeNestedObjects);
 for (UObject* Object : Nested) if(auto* Table=Cast<UChooserTable>(Object)) Tables.AddUnique(Table);
 int32 Removed=0;
 for (UChooserTable* Table : Tables)
 {
  TArray<int32> Rows;
  for (auto& Struct : Table->ColumnsStructs)
  {
   auto* Column=Struct.GetMutablePtr<FEnumColumn>();
   auto* Parameter=Column?Column->InputValue.GetMutablePtr<FEnumContextProperty>():nullptr;
   if (!Parameter || Parameter->Binding.PropertyBindingChain.Num()!=1) continue;
   const bool bStance=Parameter->Binding.PropertyBindingChain[0]==TEXT("Stance");
   // This source table's Any row is a pose-matched crouch->stand fallback.
   // There are no stance changes in Project J's standing-only locomotion contract.
   const bool bStanceTransition=Table->GetName()==TEXT("Stand Stance Transitions") && Parameter->Binding.PropertyBindingChain[0]==TEXT("Stance_LastFrame");
   if (!bStance && !bStanceTransition) continue;
   for (int32 Row=0;Row<Column->RowValues.Num();++Row)
    if (((Column->RowValues[Row].Comparison==EEnumColumnCellValueComparison::MatchEqual && Column->RowValues[Row].Value!=0) || (Column->RowValues[Row].Comparison==EEnumColumnCellValueComparison::MatchNotEqual && Column->RowValues[Row].Value==0)) || (bStanceTransition && Column->RowValues[Row].Comparison==EEnumColumnCellValueComparison::MatchAny)) Rows.AddUnique(Row);
  }
  Rows.Sort(TGreater<int32>());
  if (Rows.IsEmpty()) continue;
  Table->Modify();
  for (int32 Row : Rows)
  {
   if (!Table->ResultsStructs.IsValidIndex(Row)) return INDEX_NONE;
   Table->ResultsStructs.RemoveAt(Row);
   if (Table->DisabledRows.IsValidIndex(Row)) Table->DisabledRows.RemoveAt(Row);
  }
  for (auto& Column : Table->ColumnsStructs) if (Column.IsValid()) Column.GetMutable<FChooserColumnBase>().DeleteRows(Rows);
  Table->Compile(true); Removed+=Rows.Num();
 }
 Root->PostEditChange(); Root->MarkPackageDirty();
 return Removed;
}

int32 UProject_JPlayerLocomotionLibrary::CleanImportedBlueprintReferences(UBlueprint* Source,UBlueprint* Target)
{
 if (!Target || (!Target->GetPathName().StartsWith(TEXT("/Game/GASP58Native/")) && !Target->GetPathName().StartsWith(TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master")))) return INDEX_NONE;
 Target->Modify(); int32 Changed=0;
 if(Source && Source!=Target && Source->GeneratedClass && Target->GeneratedClass)
 {
  TMap<UObject*,UObject*> Map{{Source,Target},{Source->GeneratedClass,Target->GeneratedClass}};
  if(Source->SkeletonGeneratedClass && Target->SkeletonGeneratedClass) Map.Add(Source->SkeletonGeneratedClass,Target->SkeletonGeneratedClass);
  Map.Add(Source->GeneratedClass->GetDefaultObject(),Target->GeneratedClass->GetDefaultObject());
  const auto Flags=EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef;
  FArchiveReplaceObjectRef<UObject> BlueprintRefs(Target,Map,Flags); Changed+=BlueprintRefs.GetCount();
  FArchiveReplaceObjectRef<UObject> ClassRefs(Target->GeneratedClass,Map,Flags); Changed+=ClassRefs.GetCount();
 }
 TArray<UEdGraph*> Graphs; Target->GetAllGraphs(Graphs);
 for(UEdGraph* Graph:Graphs) for(UEdGraphNode* Node:Graph->Nodes)
  if(auto* Entry=Cast<UK2Node_FunctionEntry>(Node))
  {
   TArray<FName> Unused;
   for(const auto& Var:Entry->LocalVariables) if(!FBlueprintEditorUtils::IsVariableUsed(Target,Var.VarName,Graph)) Unused.Add(Var.VarName);
   if(!Unused.IsEmpty())
   {
    Entry->Modify(); Changed+=Entry->LocalVariables.RemoveAll([&](const FBPVariableDescription& Var){return Unused.Contains(Var.VarName);});
   }
  }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FBlueprintEditorUtils::RefreshAllNodes(Target);
 FKismetEditorUtilities::CompileBlueprint(Target); Target->MarkPackageDirty();
 return Target->Status==BS_Error?INDEX_NONE:Changed;
}

int32 UProject_JPlayerLocomotionLibrary::RebindGameplayRetargeter(UAnimBlueprint* Target,UIKRetargeter* Previous,UIKRetargeter* Replacement)
{
 if(!Target || !Previous || !Replacement || Previous==Replacement || !Target->GetPathName().StartsWith(TEXT("/Game/Animation_Logic/ABPs/GreatSword/"))) return INDEX_NONE;
 Target->Modify(); TMap<UObject*,UObject*> Map{{Previous,Replacement}};
 const auto Flags=EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef;
 FArchiveReplaceObjectRef<UObject> GraphReferences(Target,Map,Flags);
 int32 Changed=GraphReferences.GetCount();
 if(Target->GeneratedClass)
 {
  FArchiveReplaceObjectRef<UObject> ClassReferences(Target->GeneratedClass,Map,Flags); Changed+=ClassReferences.GetCount();
  FArchiveReplaceObjectRef<UObject> Defaults(Target->GeneratedClass->GetDefaultObject(),Map,Flags); Changed+=Defaults.GetCount();
 }
 // Object-valued BP variable defaults are exported text, rather than UObject fields.
 for(auto& Var:Target->NewVariables)
  if(Var.VarType.PinCategory==UEdGraphSchema_K2::PC_Object && Var.VarType.PinSubCategoryObject.Get()==UIKRetargeter::StaticClass() && Var.DefaultValue.Contains(Previous->GetPathName()))
  { Var.DefaultValue=Var.DefaultValue.Replace(*Previous->GetPathName(),*Replacement->GetPathName()); ++Changed; }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FBlueprintEditorUtils::RefreshAllNodes(Target);
 FKismetEditorUtilities::CompileBlueprint(Target); Target->MarkPackageDirty();
 return Target->Status==BS_Error?INDEX_NONE:Changed;
}
