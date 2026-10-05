#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JAnimGraphNode_GuidedHandIK.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Kismet2/CompilerResultsLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGuidedIKAuthoringTest, "ProjectJ.Maturity.Editor.GuidedIKCompileValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGuidedIKAuthoringTest::RunTest(const FString&)
{
	auto* Skeleton = NewObject<USkeleton>();
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("upper"), TEXT("upper"), 0), FTransform(FVector(10, 0, 0)));
		Modifier.Add(FMeshBoneInfo(TEXT("fore"), TEXT("fore"), 1), FTransform(FVector(10, 0, 0)));
		Modifier.Add(FMeshBoneInfo(TEXT("hand"), TEXT("hand"), 2), FTransform(FVector(10, 0, 0)));
	}
	auto* GraphNode = NewObject<UProject_JAnimGraphNode_GuidedHandIK>(); GraphNode->Node.HandBone.BoneName = TEXT("hand");
	FCompilerResultsLog Good; GraphNode->ValidateAnimNodeDuringCompilation(Skeleton, Good);
	TestEqual(TEXT("Valid static chain compiles using the runtime resolver"), Good.NumErrors, 0);
	GraphNode->Node.ForearmBone.BoneName = TEXT("fore");
	AddExpectedError(TEXT("Guided Hand IK requires a valid upper/forearm/hand chain"), EAutomationExpectedErrorFlags::Contains, 1);
	FCompilerResultsLog Incomplete; GraphNode->ValidateAnimNodeDuringCompilation(Skeleton, Incomplete);
	TestEqual(TEXT("An incomplete explicit role is explained at compile time"), Incomplete.NumErrors, 1);
	GraphNode->Node.UpperArmBone.BoneName = TEXT("upper"); GraphNode->Node.bUseBoneSpaceEffector = true; GraphNode->Node.EffectorSpaceBoneName = TEXT("hand");
	AddExpectedError(TEXT("Guided Hand IK effector reference cannot depend on this solved arm's subtree"), EAutomationExpectedErrorFlags::Contains, 1);
	FCompilerResultsLog Dependent; GraphNode->ValidateAnimNodeDuringCompilation(Skeleton, Dependent);
	TestEqual(TEXT("Self-dependent effector is rejected"), Dependent.NumErrors, 1);
	GraphNode->Node.ArmDefinitionSource = EProject_JGuidedArmDefinitionSource::SecondaryBodyProfile;
	FCompilerResultsLog Dynamic; GraphNode->ValidateAnimNodeDuringCompilation(Skeleton, Dynamic);
	TestEqual(TEXT("Dynamic profile mode does not validate irrelevant node defaults"), Dynamic.NumErrors, 0);
	return true;
}
#endif
