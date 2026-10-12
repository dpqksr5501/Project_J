#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_Base.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJObsoleteMasterFunctionsTest,
	"ProjectJ.Animation.A_Prepare.ObsoleteMasterFunctions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJObsoleteMasterFunctionsTest::RunTest(const FString&)
{
	auto* BP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
	if (!TestNotNull(TEXT("Production Master"), BP)) return false;
	const TSet<FName> Legacy = {TEXT("OnUpdate"), TEXT("OnStateEntry_IdleLoop"), TEXT("OnStateEntry_TransitionToLocomotion"),
		TEXT("OnStateEntry_LocomotionLoop"), TEXT("OnStateEntry_TransitionToIdle"), TEXT("OnStateEntry_TransitionToInAir"),
		TEXT("OnStateEntry_InAirLoop"), TEXT("OnStateEntry_TurnInPlace"), TEXT("OnUpdate_StateMachineBlendStack")};
	TArray<UEdGraph*> Removal;
	for (UEdGraph* Graph : BP->FunctionGraphs) if (Legacy.Contains(FName(*Graph->GetName().TrimStartAndEnd()))) Removal.Add(Graph);
	TArray<UBlueprint*> Related = {BP};
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FName> Referencers;
	Registry.GetReferencers(BP->GetOutermost()->GetFName(), Referencers);
	for (FName Package : Referencers)
	{
		if (!Package.ToString().StartsWith(TEXT("/Game/Animation_Logic/ABPs/"))) continue;
		const FString Object = Package.ToString() + TEXT(".") + FPackageName::GetShortName(Package);
		if (auto* Child = LoadObject<UAnimBlueprint>(nullptr, *Object)) Related.AddUnique(Child);
	}
	FString Report;
	bool bReferenced = false;
	for (auto* Asset : Related)
	{
		TArray<UEdGraph*> Graphs; Asset->GetAllGraphs(Graphs);
		for (auto* Graph : Graphs)
		{
			if (Asset == BP && Removal.Contains(Graph)) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (auto* Anim = Cast<UAnimGraphNode_Base>(Node)) Report += FString::Printf(TEXT("Callback %s Initial=%s Relevant=%s Update=%s\n"),
					*Node->GetPathName(), *Anim->InitialUpdateFunction.GetMemberName().ToString(),
					*Anim->BecomeRelevantFunction.GetMemberName().ToString(), *Anim->UpdateFunction.GetMemberName().ToString());
				// Includes calls, delegate names, node callbacks and exposed-pin binding structs.
				for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
				{
					FString Value; It->ExportText_InContainer(0, Value, Node, nullptr, Node, PPF_None);
					for (const FName Name : Legacy)
					{
						FRegexMatcher Match(FRegexPattern(TEXT("(^|[^A-Za-z0-9_])") + Name.ToString() + TEXT("([^A-Za-z0-9_]|$)")), Value);
						if (!Match.FindNext()) continue;
						bReferenced = true;
						Report += FString::Printf(TEXT("Reference %s Property=%s Function=%s\n"), *Node->GetPathName(), *It->GetName(), *Name.ToString());
					}
				}
			}
		}
	}
	for (auto* Graph : Removal) Report += FString::Printf(TEXT("ObsoleteFunction %s Nodes=%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	AddInfo(FString::Printf(TEXT("Master legacy functions=%d Related Blueprints=%d Live reference=%d"), Removal.Num(), Related.Num(), bReferenced));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/ContinuousCurve_20261012");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FFileHelper::SaveStringToFile(Report, *(Directory / TEXT("MasterFunctionAudit_AfterCleanup.txt")));
	if (!TestFalse(TEXT("Obsolete callbacks have no live graph references"), bReferenced)) return false;
	return TestTrue(TEXT("Production Master has no obsolete State Machine function graphs"), Removal.IsEmpty());
}
#endif
