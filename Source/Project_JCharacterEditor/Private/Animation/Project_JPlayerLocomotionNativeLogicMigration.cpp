#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Movement/Project_JGroundMovement.h"
#include "Animation/AnimBlueprint.h"
#include "Chooser.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
bool NativeAdapter(UBlueprint* Blueprint,FName Name,UFunction* Native)
{
	if (!Blueprint || !Native) return false;
	TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
	UEdGraph* Graph=nullptr;for (auto* Candidate:Graphs) if (Candidate->GetFName()==Name) {Graph=Candidate;break;}
	if (!Graph) return false;
	UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;
	for (UEdGraphNode* Node:Graph->Nodes)
	{
		if (auto* Candidate=Cast<UK2Node_FunctionEntry>(Node)) Entry=Candidate;
		if (!Result) Result=Cast<UK2Node_FunctionResult>(Node);
	}
	if (!Entry) return false;
	Graph->Modify();
	for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
	{
		Node->BreakAllNodeLinks();
		if (Node!=Entry && Node!=Result) FBlueprintEditorUtils::RemoveNode(Blueprint,Node,true);
	}
	Entry->LocalVariables.Reset();
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Call=Creator.CreateNode();Call->SetFromFunction(Native);Creator.Finalize();
	const auto* Schema=Graph->GetSchema();
	auto Connect=[&](UEdGraphPin* A,UEdGraphPin* B)
	{
		if (A && B && Schema->TryCreateConnection(A,B)) return true;
		UE_LOG(LogTemp,Error,TEXT("Native adapter %s connection failed: %s -> %s (%s)"),*Name.ToString(),A?*A->PinName.ToString():TEXT("missing"),B?*B->PinName.ToString():TEXT("missing"),A && B?*Schema->CanCreateConnection(A,B).Message.ToString():TEXT("pin not found"));
		return false;
	};
	if (auto* Exec=Call->GetExecPin())
	{
		if (!Connect(Entry->GetThenPin(),Exec)) return false;
		if (Result && !Connect(Call->GetThenPin(),Result->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
	}
	else if (Result && !Connect(Entry->GetThenPin(),Result->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
	for (UEdGraphPin* Pin:Call->Pins)
	{
		if (Pin->Direction!=EGPD_Input || Pin->PinType.PinCategory==UEdGraphSchema_K2::PC_Exec || Pin->PinName==UEdGraphSchema_K2::PN_Self) continue;
		if (Pin->PinName==TEXT("Mode"))
		{
			FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);auto* Self=SelfCreator.CreateNode();SelfCreator.Finalize();
			if (!Connect(Self->FindPin(UEdGraphSchema_K2::PN_Self),Pin)) return false;
			continue;
		}
		UEdGraphPin* Input=Entry->FindPin(Pin->PinName);
		if (!Input && Pin->PinName==TEXT("Node")) Input=Entry->FindPin(TEXT("BlendStackInput"));
		if (!Input && Pin->PinName==TEXT("StateName")) Input=Entry->FindPin(TEXT("State Name"));
		if (!Input || !Connect(Input,Pin)) return false;
	}
	TArray<UEdGraphPin*> Outputs;
	if (Result) for (UEdGraphPin* Pin:Result->Pins)
		if (Pin->Direction==EGPD_Input && Pin->PinType.PinCategory!=UEdGraphSchema_K2::PC_Exec && !Pin->ParentPin) Outputs.Add(Pin);
	for (UEdGraphPin* Pin:Outputs)
	{
		if (Pin->Direction!=EGPD_Input || Pin->PinType.PinCategory==UEdGraphSchema_K2::PC_Exec || Pin->ParentPin) continue;
		if (!Pin->SubPins.IsEmpty()) Schema->RecombinePin(Pin);
		auto* Output=Call->FindPin(Pin->PinName);
		if (!Output) Output=Call->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
		if (!Connect(Output,Pin)) return false;
	}
	return true;
}
}

bool UProject_JPlayerLocomotionLibrary::InstallNativeLocomotionLogic(UAnimBlueprint* Target,UBlueprint* GroundMode)
{
	if (!Target || !GroundMode || !Target->ParentClass || !Target->ParentClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass())) return false;
	for (UEdGraph* Graph:Target->UbergraphPages) for (UEdGraphNode* Node:Graph->Nodes)
		if (const auto* Event=Cast<UK2Node_Event>(Node);Event && Event->EventReference.GetMemberName()!=TEXT("BlueprintPostEvaluateAnimation")) return false;
	struct FAdapter {const TCHAR* Graph;FName Function;};
	const FAdapter Adapters[]={
		{TEXT("Update_Logic"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeLocomotionLogic)},
		{TEXT("Update_Trajectory"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeTrajectory)},
		{TEXT("Update_EssentialValues"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeEssentialValues)},
		{TEXT("Update_States"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeStates)},
		{TEXT("Update_AimOffset"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeAimOffset)},
		{TEXT("Update_AdditiveLean"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeAdditiveLean)},
		{TEXT("OnUpdate_LocomotionTransition"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeLocomotionTransition)},
		{TEXT("IsMoving"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateIsMoving)},
		{TEXT("IsStarting"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateIsStarting)},
		{TEXT("IsPivoting"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateIsPivoting)},
		{TEXT("ShouldTurnInPlace"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTurnInPlace)},
		{TEXT("ShouldSpinTransition"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateSpinTransition)},
		{TEXT("JustLanded_Light"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateLightLanding)},
		{TEXT("JustLanded_Heavy"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateHeavyLanding)},
		{TEXT("JustTeleported"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTeleport)},
		{TEXT("JustTraversed"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTraversed)},
		{TEXT("Get_TrajectoryTurnAngle"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTrajectoryTurnAngle)},
		{TEXT("Get_TotalFacingDelta"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateTotalFacingDelta)},
		{TEXT("Get_LandVelocity"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateLandVelocity)},
		{TEXT("Get_BlendSpaceInputs"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateBlendSpaceInputs)},
		{TEXT("Get_StrafeWarpDirection"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateStrafeWarpDirection)},
		{TEXT("AllowFootPinning"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateFootPinning)},
		{TEXT("AllowSlopeWarping"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateSlopeWarping)},
		{TEXT("Get_OffsetRootRotationMode"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateRootRotationMode)},
		{TEXT("Get_OffsetRootTranslationMode"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateRootTranslationMode)},
		{TEXT("Get_OffsetRootTranslationHalfLife"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateRootTranslationHalfLife)},
		{TEXT("Get_OffsetRootTranslationRadius"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateRootTranslationRadius)},
		{TEXT("Get_OrientationWarpingWarpingSpace"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateOrientationWarpingSpace)},
		{TEXT("Get_DynamicPlayRate"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateDynamicPlayRate)},
		{TEXT("Get_StrideWarpAlpha"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateWarpAlpha)},
		{TEXT("Get_StrafeWarpAlpha"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateWarpAlpha)},
		{TEXT("Get_ProceduralTargetTime"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateProceduralTargetTime)},
		{TEXT("EnableSteering"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateSteering)}
		,{TEXT("Get_DesiredFacing"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateDesiredFacing)}
		,{TEXT("IsAnimationAlmostComplete"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EvaluateAnimationAlmostComplete)}
		,{TEXT("SetBlendStackAnimFromChooser"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,SelectNativeStateAnimation)}
		,{TEXT("OnStateEntry_LocomotionTransition"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeLocomotionTransition)}
		,{TEXT("OnStateEntry_InAirTransition"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirTransition)}
		,{TEXT("OnStateEntry_InAirLoop"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirLoop)}
		,{TEXT("OnUpdate_OffsetRootBone"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateNativeRootOffset)}
		,{TEXT("Biped_FootPlacement_OnBecomeRelevant"),GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,ResetNativeFootPlacement)}
	};
	for (const auto& Adapter:Adapters)
	{
		// Cleanup retires these uncalled BP update wrappers; native lifecycle invokes
		// their C++ implementations directly. Keep query/pose callback contracts strict.
		const FString Name(Adapter.Graph);
		const bool bRetiredUpdate=Name==TEXT("Update_Logic") || Name==TEXT("Update_Trajectory") || Name==TEXT("Update_EssentialValues") || Name==TEXT("Update_States") || Name==TEXT("Update_AimOffset") || Name==TEXT("Update_AdditiveLean");
		TArray<UEdGraph*> Graphs;Target->GetAllGraphs(Graphs);
		if (bRetiredUpdate && !Graphs.ContainsByPredicate([&](const auto* Graph){return Graph->GetName()==Name;})) continue;
		// The current typed contract binds pure native queries directly. Do not
		// recreate VM forwarding graphs retired by SimplifyNativeLocomotionQueries.
		const auto* Native=UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(Adapter.Function);
		if (Target->NewVariables.IsEmpty() && Native && Native->HasAnyFunctionFlags(FUNC_BlueprintPure) &&
			!Graphs.ContainsByPredicate([&](const auto* Graph){return Graph->GetName()==Name;})) continue;
		if (!NativeAdapter(Target,Adapter.Graph,UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(Adapter.Function)))
		{ UE_LOG(LogTemp,Error,TEXT("Native locomotion adapter failed: %s -> %s"),Adapter.Graph,*Adapter.Function.ToString());return false; }
	}
	if (!NativeAdapter(GroundMode,TEXT("GenerateWalkMove"),UProject_JGroundMovementLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UProject_JGroundMovementLibrary,GenerateGroundMove)))) return false;
	// The native simulation owns the landing window; no latent game-thread delay remains.
	for (UEdGraph* Graph:GroundMode->UbergraphPages)
		for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes)) FBlueprintEditorUtils::RemoveNode(GroundMode,Node,true);
	// Native lifecycle callbacks own the per-frame update and post-evaluate reset.
	TArray<UEdGraph*> TargetGraphs;Target->GetAllGraphs(TargetGraphs);
	for (auto* Graph:TargetGraphs)
		if (Graph->GetFName()==TEXT("BlueprintThreadSafeUpdateAnimation") || Graph->GetFName()==TEXT("Update_CVarDrivenVariables")) FBlueprintEditorUtils::RemoveGraph(Target,Graph,EGraphRemoveFlags::None);
	for (UEdGraph* Graph:Target->UbergraphPages)
		for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes)) FBlueprintEditorUtils::RemoveNode(Target,Node,true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GroundMode);FKismetEditorUtilities::CompileBlueprint(GroundMode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FKismetEditorUtilities::CompileBlueprint(Target);
	if (auto* Defaults=Cast<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject()))
	{
		if(!Defaults->GroundAnimationChooser)
			Defaults->GroundAnimationChooser=LoadObject<UChooserTable>(nullptr,TEXT("/Game/Animation_Logic/Chooser/CHT_ProjectJ_Ground.CHT_ProjectJ_Ground"));
		if (!Defaults->GroundAnimationChooser) return false;
	}
	return GroundMode->Status!=BS_Error && Target->Status!=BS_Error;
}
