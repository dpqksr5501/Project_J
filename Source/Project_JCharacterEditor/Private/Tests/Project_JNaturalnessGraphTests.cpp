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
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
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

// Explicit one-asset repair. Ordinary test runs never compile or save assets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNaturalnessExternalWarpingRepair, "ProjectJ.Authoring.Naturalness.ExternalWarpingRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNaturalnessExternalWarpingRepair::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJRepairExternalWarping")))
	{
		AddInfo(TEXT("Repair skipped: explicit -ProjectJRepairExternalWarping required."));
		return true;
	}
	auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Repair target"), Blueprint) ||
		!TestFalse(TEXT("Refuse to overwrite an already dirty Master"), Blueprint->GetOutermost()->IsDirty())) return false;
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UEdGraphNode* Warping = nullptr;
	UEdGraphNode* Steering = nullptr;
	for (auto* Graph : Graphs)
	{
		if (!Graph->GetPathName().Contains(TEXT("AnimGraphNode_BlendStack_0.AnimationBlendStackGraph_0"))) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->GetClass()->GetName() == TEXT("AnimGraphNode_OrientationWarping")) Warping = Node;
			if (Node->GetClass()->GetName() == TEXT("AnimGraphNode_Steering")) Steering = Node;
		}
	}
	if (!TestNotNull(TEXT("External stack OW"), Warping) || !TestNotNull(TEXT("Same player's Steering"), Steering)) return false;
	auto* Alpha = Warping->FindPin(TEXT("Alpha"));
	auto* Time = Warping->FindPin(TEXT("CurrentAnimAssetTime"));
	auto* PlayerTime = Steering->FindPin(TEXT("CurrentAnimAssetTime"));
	if (!Alpha || !Time || !PlayerTime || Alpha->LinkedTo.Num() != 1 || Time->LinkedTo.Num() != 1 || PlayerTime->LinkedTo.Num() != 1)
	{
		AddError(TEXT("Unexpected pin topology; no change made.")); return false;
	}
	auto* CurveProduct = Time->LinkedTo[0];
	auto* ActualTime = PlayerTime->LinkedTo[0];
	if (Alpha->LinkedTo[0]->GetOwningNode()->GetName() == TEXT("K2Node_PromotableOperator_0") &&
		Time->LinkedTo[0] == ActualTime)
	{
		AddInfo(TEXT("External warp contract already repaired; nothing saved.")); return true;
	}
	if (!TestEqual(TEXT("Before: curve product incorrectly feeds time"), CurveProduct->GetOwningNode()->GetName(), FString(TEXT("K2Node_PromotableOperator_0"))) ||
		!TestEqual(TEXT("Before: Alpha only has native eligibility"), Alpha->LinkedTo[0]->GetOwningNode()->GetName(), FString(TEXT("K2Node_Knot_0"))) ||
		!TestEqual(TEXT("Actual per-player time source"), ActualTime->GetOwningNode()->GetName(), FString(TEXT("K2Node_Knot_3")))) return false;
	AddInfo(TEXT("Before: Alpha=native gate; CurrentAnimAssetTime=enable_warping(asset, player time)*native gate; TargetTime=0. After: Alpha=curve*gate; CurrentAnimAssetTime=player time. Only Master will be compiled/saved."));
	Blueprint->Modify(); Warping->Modify();
	const UEdGraphSchema* Schema = Warping->GetGraph()->GetSchema();
	Schema->BreakPinLinks(*Alpha, true);
	Schema->BreakPinLinks(*Time, true);
	if (!TestTrue(TEXT("Connect authored curve product to Alpha"), Schema->TryCreateConnection(CurveProduct, Alpha)) ||
		!TestTrue(TEXT("Connect actual player time to extraction time"), Schema->TryCreateConnection(ActualTime, Time))) return false;
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	if (!TestEqual(TEXT("Changed Master compiles without errors"), Results.NumErrors, 0)) return false;
	const FString Filename = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
	TestTrue(TEXT("Saved only the changed Master"), UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs));
	return true;
}
#endif
