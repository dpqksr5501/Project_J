#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "AnimGraphNode_Base.h"
#include "K2Node_CallFunction.h"
#include "UObject/UnrealType.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNaturalnessActiveGraphAudit, "ProjectJ.Animation.Naturalness.ActiveGraphAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNaturalnessActiveGraphAudit::RunTest(const FString&)
{
	FString Report;
	const auto RecordDirty = [&](const TCHAR* Stage)
	{
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if (It->IsDirty() && It->GetName().StartsWith(TEXT("/Game/")))
			{
				Report += FString::Printf(TEXT("DirtyPackage Stage=%s Path=%s\n"), Stage, *It->GetName());
			}
		}
	};
	RecordDirty(TEXT("BeforeLoad"));
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production pawn class"), Class)) return false;
	const auto* Pawn = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
	const auto* AnimClass = Pawn->GetMesh()->GetAnimClass();
	TestEqual(TEXT("Production uses the humanoid Master"), AnimClass->GetPathName(),
		FString(TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master_C")));
	Report += FString::Printf(TEXT("Pawn=%s AnimClass=%s Profile=%s AssetSet=%s CombatAssetSet=%s\n"),
		*Class->GetPathName(), *AnimClass->GetPathName(), *GetPathNameSafe(Pawn->GetLocomotionProfile()),
		*GetPathNameSafe(Pawn->GetMotionMatchingAssetSet()), *GetPathNameSafe(Pawn->GetCombatStrafeMotionMatchingAssetSet()));
	for (const auto* Set : {Pawn->GetMotionMatchingAssetSet(), Pawn->GetCombatStrafeMotionMatchingAssetSet()})
	{
		if (!TestNotNull(TEXT("Production asset set"), Set)) continue;
		for (const auto* DB : {Set->RunDatabases.Cycle.Get(), Set->RunDatabases.TurnRedirect.Get(),
			Set->SprintDatabases.Cycle.Get()})
		{
			if (DB) Report += FString::Printf(TEXT("Database=%s Schema=%s Entries=%d\n"),
				*DB->GetPathName(), *GetPathNameSafe(DB->Schema.Get()), DB->GetNumAnimationAssets());
		}
	}
	auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Production animation blueprint"), Blueprint)) return false;
	Report += FString::Printf(TEXT("MasterDirtyAfterLoad=%d Status=%d\n"), Blueprint->GetOutermost()->IsDirty(), int32(Blueprint->Status));
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (auto* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectWarpingContract")) &&
				Node->GetClass()->GetName() == TEXT("AnimGraphNode_OrientationWarping"))
			{
				auto* Alpha = Node->FindPin(TEXT("Alpha"));
				auto* Time = Node->FindPin(TEXT("CurrentAnimAssetTime"));
				TestTrue(TEXT("Authored warp curve controls Alpha"), Alpha && Alpha->LinkedTo.Num() == 1 &&
					Alpha->LinkedTo[0]->GetOwningNode()->GetName() == TEXT("K2Node_PromotableOperator_0"));
				TestTrue(TEXT("Warp extraction time is the current stack player's time"), Time && Time->LinkedTo.Num() == 1 &&
					Time->LinkedTo[0]->GetOwningNode()->GetName() == TEXT("K2Node_Knot_3"));
			}
			// Preserve current connectivity, including internal stack graphs, without compiling/saving.
			Report += FString::Printf(TEXT("Node=%s Class=%s\n"), *Node->GetPathName(), *Node->GetClass()->GetName());
			if (const auto* AnimNode = Cast<UAnimGraphNode_Base>(Node))
			{
				Report += FString::Printf(TEXT("  Functions Initial=%s Relevant=%s Update=%s\n"),
					*AnimNode->InitialUpdateFunction.GetMemberName().ToString(),
					*AnimNode->BecomeRelevantFunction.GetMemberName().ToString(),
					*AnimNode->UpdateFunction.GetMemberName().ToString());
				// Only settings relevant to branch handoff; never export/modify the asset.
				if (const auto* RuntimeProperty = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node")))
				{
					const void* Runtime = RuntimeProperty->ContainerPtrToValuePtr<void>(Node);
					for (const TCHAR* Name : {TEXT("bResetOnBecomingRelevant"), TEXT("BlendTime"), TEXT("SearchThrottleTime"),
						TEXT("bUseInertialBlend"), TEXT("TransitionType"), TEXT("bResetChildOnActivation"),
						TEXT("PoseCount"), TEXT("SamplingInterval")})
					{
						if (const FProperty* Setting = FindFProperty<FProperty>(RuntimeProperty->Struct, Name))
						{
							FString Value;
							Setting->ExportTextItem_Direct(Value, Setting->ContainerPtrToValuePtr<void>(Runtime), nullptr, Node, PPF_None);
							Report += FString::Printf(TEXT("  Setting %s=%s\n"), Name, *Value);
						}
					}
				}
			}
			if (const auto* Call = Cast<UK2Node_CallFunction>(Node))
			{
				Report += FString::Printf(TEXT("  Function=%s\n"), *Call->FunctionReference.GetMemberName().ToString());
			}
			for (auto* Pin : Node->Pins)
			{
				if (!Pin) continue;
				Report += FString::Printf(TEXT("  Pin=%s Direction=%d Default=%s Links="), *Pin->PinName.ToString(), int32(Pin->Direction), *Pin->DefaultValue);
				for (auto* Link : Pin->LinkedTo)
				{
					Report += Link->GetOwningNode()->GetPathName() + TEXT(".") + Link->PinName.ToString() + TEXT(";");
				}
				Report += TEXT("\n");
			}
		}
	}
	RecordDirty(TEXT("AfterLoad"));
	FString Output;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJNaturalnessGraphOutput="), Output);
	if (Output.IsEmpty()) Output = FPaths::ProjectSavedDir() / TEXT("Validation/Naturalness_20261006/ActiveGraph.txt");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	TestTrue(TEXT("Wrote read-only production graph audit"), FFileHelper::SaveStringToFile(Report, *Output));
	AddInfo(TEXT("Graph audit: ") + Output);
	return true;
}

// Retained name/flag for historical command compatibility. The 2026-10-06
// external BlendStack repair is not a migration for the current unified graph.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNaturalnessExternalWarpingRepair, "ProjectJ.Authoring.Naturalness.ExternalWarpingRepair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNaturalnessExternalWarpingRepair::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJRepairExternalWarping")))
    {
        AddInfo(TEXT("Not applicable: historical external-stack repair is retired; no asset loaded or saved."));
        return true;
    }
    AddError(TEXT("Retired external-stack repair cannot modify the current unified locomotion graph. Reuse requires a reviewed migration on an isolated asset copy."));
    return false;
}
#endif
