#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_ResetRoot.h"
#include "AnimGraph/AnimGraphNode_Steering.h"
#include "AnimGraphNode_BlendStackInput.h"
#include "AnimGraph/AnimGraphNode_OffsetRootBone.h"
#include "K2Node_AnimNodeReference.h"
#include "K2Node_CallFunction.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace ProjectJContinuityGraph
{
template<class T> T* Make(UEdGraph* Graph, int32 X, int32 Y)
{
	auto* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Node, false, false); Node->CreateNewGuid(); Node->PostPlacedNewNode();
	Node->NodePosX = X; Node->NodePosY = Y; static_cast<UEdGraphNode*>(Node)->AllocateDefaultPins();
	return Node;
}
UK2Node_CallFunction* Call(UEdGraph* Graph, UClass* Class, FName Function, int32 X, int32 Y)
{
	auto* Node = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
	Node->SetFromFunction(Class->FindFunctionByName(Function)); Graph->AddNode(Node, false, false);
	Node->CreateNewGuid(); Node->PostPlacedNewNode(); Node->NodePosX = X; Node->NodePosY = Y; Node->AllocateDefaultPins();
	return Node;
}
void Expose(UAnimGraphNode_Base* Node, FName Name)
{
	for (auto& Optional : Node->ShowPinForProperties) if (Optional.PropertyName == Name) Optional.bShowPin = true;
	Node->ReconstructNode();
}
UEdGraphNode* Find(UEdGraph* Graph, const TCHAR* ClassName)
{
	for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->GetClass()->GetName() == ClassName) return Node;
	return nullptr;
}
bool Insert(UEdGraph* Graph, UEdGraphPin* From, UEdGraphPin* To, UAnimGraphNode_BlendStackInput* Input,
	FAutomationTestBase* Test, int32 X)
{
	const auto* Schema = Graph->GetSchema();
	bool bOK = true;
	const auto Link = [&](UEdGraphPin* A, UEdGraphPin* B)
	{
		bOK &= Test->TestTrue(TEXT("Connect continuity graph pins"), A && B && Schema->TryCreateConnection(A, B));
	};
	auto* Reset = Make<UAnimGraphNode_ResetRoot>(Graph, X, 0);
	auto* Steering = Make<UAnimGraphNode_Steering>(Graph, X + 240, 0);
	Steering->Node.DisableSteeringBelowSpeed = 10.0f;
	Expose(Reset, TEXT("Alpha")); Expose(Steering, TEXT("Alpha"));
	Steering->NodeComment = TEXT("ProjectJ local locomotion continuity; each blend player's asset/time/mirror");
	Schema->BreakSinglePinLink(From, To);
	Link(From, Reset->FindPin(TEXT("ComponentPose")));
	Link(Reset->FindPin(TEXT("Pose")), Steering->FindPin(TEXT("ComponentPose")));
	Link(Steering->FindPin(TEXT("Pose")), To);
	const FName Tag = Input->GetTag();
	if (!Test->TestFalse(TEXT("Per-player input has a unique tag"), Tag.IsNone())) return false;
	auto* Reference = Make<UK2Node_AnimNodeReference>(Graph, X - 200, 400); Reference->SetTag(Tag);
	int32 Y = 240;
	for (const auto& Pair : {TPair<FName,FName>(TEXT("GetThreadSafeLocomotionSteeringAlpha"), TEXT("Alpha")),
		{TEXT("GetThreadSafeLocomotionSteeringTarget"), TEXT("TargetOrientation")},
		{TEXT("GetThreadSafeLocomotionSteeringProceduralTime"), TEXT("ProceduralTargetTime")},
		{TEXT("GetThreadSafeLocomotionSteeringAnimatedTime"), TEXT("AnimatedTargetTime")}})
	{
		auto* Getter = Call(Graph, UProject_JCharacterAnimInstance::StaticClass(), Pair.Key, X + 500, Y);
		Link(Getter->FindPin(TEXT("ReturnValue")), Steering->FindPin(Pair.Value));
		if (Pair.Value == TEXT("Alpha")) Link(Getter->FindPin(TEXT("ReturnValue")), Reset->FindPin(TEXT("Alpha")));
		Y += 140;
	}
	for (const auto& Pair : {TPair<FName,FName>(TEXT("GetCurrentBlendStackAnimAsset"), TEXT("CurrentAnimAsset")),
		{TEXT("GetCurrentBlendStackAnimAssetTime"), TEXT("CurrentAnimAssetTime")},
		{TEXT("GetCurrentBlendStackAnimAssetMirrored"), TEXT("bMirrored")},
		{TEXT("GetCurrentBlendStackAnimAssetMirrorTable"), TEXT("MirrorDataTable")}})
	{
		auto* Getter = Call(Graph, UBlendStackAnimNodeLibrary::StaticClass(), Pair.Key, X, Y);
		Link(Reference->FindPin(TEXT("Value")), Getter->FindPin(TEXT("Node")));
		Link(Getter->FindPin(TEXT("ReturnValue")), Steering->FindPin(Pair.Value)); Y += 140;
	}
	return bOK;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionContinuityGraphTest, "ProjectJ.LocomotionContinuity.ProductionGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionContinuityGraphTest::RunTest(const FString&)
{
	using namespace ProjectJContinuityGraph;
	auto* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Production Master"), Blueprint)) return false;
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UEdGraph* MM = nullptr; UEdGraph* External = nullptr;
	UAnimGraphNode_OffsetRootBone* Offset = nullptr;
	for (auto* Graph : Graphs)
	{
		if (Graph->GetPathName().Contains(TEXT("AnimGraphNode_MotionMatching_0.AnimationBlendStackGraph_0"))) MM = Graph;
		if (Graph->GetPathName().Contains(TEXT("AnimGraphNode_BlendStack_0.AnimationBlendStackGraph_0"))) External = Graph;
		for (UEdGraphNode* Node : Graph->Nodes) if (auto* Candidate = Cast<UAnimGraphNode_OffsetRootBone>(Node)) Offset = Candidate;
	}
	if (!TestNotNull(TEXT("MM per-player graph"), MM) || !TestNotNull(TEXT("External per-player graph"), External) ||
		!TestNotNull(TEXT("Existing offset consumer"), Offset)) return false;
	const bool bInstalled = Find(MM, TEXT("AnimGraphNode_Steering")) != nullptr;
	const bool bApply = FParse::Param(FCommandLine::Get(), TEXT("ProjectJApplyLocomotionContinuity"));
	if (!bInstalled && bApply)
	{
		if (!TestFalse(TEXT("Refuse already dirty Master"), Blueprint->GetOutermost()->IsDirty())) return false;
		auto* Input = Cast<UAnimGraphNode_BlendStackInput>(Find(MM, TEXT("AnimGraphNode_BlendStackInput")));
		auto* Result = Find(MM, TEXT("AnimGraphNode_BlendStackResult"));
		auto* ExternalInput = Cast<UAnimGraphNode_BlendStackInput>(Find(External, TEXT("AnimGraphNode_BlendStackInput")));
		auto* TIP = Find(External, TEXT("AnimGraphNode_Steering"));
		if (!Input || !Result || !ExternalInput || !TIP) { AddError(TEXT("Unexpected production topology")); return false; }
		auto* From = Input->FindPin(TEXT("Pose")); auto* To = Result->FindPin(TEXT("Result"));
		auto* TIPInput = TIP->FindPin(TEXT("ComponentPose"));
		if (!From || !To || From->LinkedTo.Num() != 1 || From->LinkedTo[0] != To ||
			!TIPInput || TIPInput->LinkedTo.Num() != 1) { AddError(TEXT("Unexpected links; no asset saved")); return false; }
		Blueprint->Modify(); MM->Modify(); External->Modify();
		Input->SetTag(TEXT("ProjectJLocomotionStackInput"));
		auto* L2C = Make<UAnimGraphNode_LocalToComponentSpace>(MM, 200, 0);
		auto* C2L = Make<UAnimGraphNode_ComponentToLocalSpace>(MM, 1100, 0);
		const auto* Schema = MM->GetSchema(); Schema->BreakSinglePinLink(From, To);
		if (!TestTrue(TEXT("MM input conversion"), Schema->TryCreateConnection(From, L2C->FindPin(TEXT("LocalPose")))) ||
			!TestTrue(TEXT("MM output conversion"), Schema->TryCreateConnection(C2L->FindPin(TEXT("Pose")), To))) return false;
		if (!Insert(MM, L2C->FindPin(TEXT("ComponentPose")), C2L->FindPin(TEXT("ComponentPose")), Input, this, 450) ||
			!Insert(External, TIPInput->LinkedTo[0], TIPInput, ExternalInput, this, 1000)) return false;
		Expose(Offset, TEXT("MaxRotationError"));
		auto* Limit = Call(Offset->GetGraph(), UProject_JCharacterAnimInstance::StaticClass(), TEXT("GetThreadSafeLocomotionSteeringMaxYawError"), Offset->NodePosX, Offset->NodePosY + 400);
		if (!TestTrue(TEXT("Bound visual yaw without changing translation"), Offset->GetGraph()->GetSchema()->TryCreateConnection(Limit->FindPin(TEXT("ReturnValue")), Offset->FindPin(TEXT("MaxRotationError"))))) return false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		if (!TestEqual(TEXT("Continuity Master compiles"), Results.NumErrors, 0)) return false;
		const FString File = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		if (!TestTrue(TEXT("Saved only Master"), UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *File, Args))) return false;
		AddInfo(TEXT("Installed MM and external per-player ResetRoot/Steering; existing TIP/OW and translation preserved."));
	}
	if (bApply || FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectLocomotionContinuity")))
	{
		for (auto* Graph : {MM, External})
		{
			int32 General = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (auto* Steering = Cast<UAnimGraphNode_Steering>(Node))
				{
					if (!Steering->NodeComment.StartsWith(TEXT("ProjectJ local locomotion continuity"))) continue;
					++General;
					for (auto Pin : {TEXT("Alpha"),TEXT("TargetOrientation"),TEXT("CurrentAnimAsset"),TEXT("CurrentAnimAssetTime"),TEXT("bMirrored"),TEXT("MirrorDataTable")})
						TestTrue(TEXT("Each player's steering inputs are connected"), Steering->FindPin(Pin) && Steering->FindPin(Pin)->LinkedTo.Num() == 1);
				}
			}
			TestEqual(TEXT("One general steering per player graph"), General, 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionContinuityNormalizationTest, "ProjectJ.LocomotionContinuity.ProductionCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionContinuityNormalizationTest::RunTest(const FString&)
{
	auto* Class = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	if (!TestNotNull(TEXT("Production player"), Class)) return false;
	const auto* Player = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
	const bool bApply = FParse::Param(FCommandLine::Get(), TEXT("ProjectJApplyLocomotionContinuity"));
	int32 FamilyIndex = 0;
	for (const auto* Set : {Player->GetMotionMatchingAssetSet(), Player->GetCombatStrafeMotionMatchingAssetSet()})
	{
		if (!TestNotNull(TEXT("Production family"), Set)) return false;
		auto* Cycle = Set->RunDatabases.Cycle.Get(); auto* Turn = Set->RunDatabases.TurnRedirect.Get();
		if (!TestNotNull(TEXT("Dynamic Cycle"), Cycle) || !TestNotNull(TEXT("Turn"), Turn) ||
			!TestTrue(TEXT("Matching production schema"), Cycle->Schema && Cycle->Schema == Turn->Schema)) return false;
		AddInfo(FString::Printf(TEXT("Cycle=%s Turn=%s Schema=%s Preprocessor=%d CycleNorm=%s TurnNorm=%s"),
			*Cycle->GetPathName(), *Turn->GetPathName(), *Cycle->Schema->GetPathName(), int32(Cycle->Schema->DataPreprocessor),
			*GetPathNameSafe(Cycle->NormalizationSet.Get()), *GetPathNameSafe(Turn->NormalizationSet.Get())));
		if (bApply && Cycle->Schema->DataPreprocessor != EPoseSearchDataPreprocessor::None &&
			(!Cycle->NormalizationSet || Cycle->NormalizationSet != Turn->NormalizationSet))
		{
			if (!TestTrue(TEXT("Refuse replacing existing authored normalization"), !Cycle->NormalizationSet && !Turn->NormalizationSet) ||
				!TestFalse(TEXT("Refuse dirty Cycle"), Cycle->GetOutermost()->IsDirty()) ||
				!TestFalse(TEXT("Refuse dirty Turn"), Turn->GetOutermost()->IsDirty())) return false;
			const FString Path = FamilyIndex == 0 ? TEXT("/Game/Animation_Logic/PSD/PSD_Player_Locomotion/NS_Run_Continuity") :
				TEXT("/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/NS_Combat_Run_Continuity");
			if (!TestNull(TEXT("New normalization path is unused"), LoadObject<UPoseSearchNormalizationSet>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)), nullptr, LOAD_NoWarn))) return false;
			for (auto* Database : {Cycle, Turn})
			{
				const FString Source = FPackageName::LongPackageNameToFilename(Database->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
				FString Relative = FPaths::ConvertRelativePathToFull(Source);
				const FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
				FPaths::MakePathRelativeTo(Relative, *ProjectDirectory);
				const FString Backup = FPaths::ProjectSavedDir() / TEXT("Validation/LocomotionContinuity_20261006/Before") / Relative;
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
				if (!IFileManager::Get().FileExists(*Backup) && !TestEqual(TEXT("Preserved original PSD"), IFileManager::Get().Copy(*Backup, *Source), COPY_OK)) return false;
			}
			auto* Package = CreatePackage(*Path);
			auto* Normalization = NewObject<UPoseSearchNormalizationSet>(Package, *FPackageName::GetShortName(Path), RF_Public | RF_Standalone);
			Normalization->Databases = {Cycle, Turn}; FAssetRegistryModule::AssetCreated(Normalization); Package->MarkPackageDirty();
			FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
			if (!TestTrue(TEXT("Saved new family normalization"), UPackage::SavePackage(Package, Normalization,
				*FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()), Args))) return false;
			for (auto* Database : {Cycle, Turn})
			{
				Database->Modify(); Database->NormalizationSet = Normalization; Database->PostEditChange();
				if (!TestTrue(TEXT("Saved only participating Run PSD"), UPackage::SavePackage(Database->GetOutermost(), Database,
					*FPackageName::LongPackageNameToFilename(Database->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args))) return false;
			}
		}
		FProject_JMotionMatchingSelectionContext Context; Context.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn; Context.bMovingTurn180 = true;
		if (FamilyIndex == 1) { Context.RotationMode = EProject_JLocomotionRotationMode::Strafe; Context.bUseGenericFamiliesForNonOrientToMovement = true; }
		if (bApply || FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectLocomotionContinuity")))
			TestEqual(TEXT("Actual production pair passes compatibility guard"), Set->FindTurnCycleCompanion(Context, Turn), Cycle);
		++FamilyIndex;
	}
	return true;
}
#endif
