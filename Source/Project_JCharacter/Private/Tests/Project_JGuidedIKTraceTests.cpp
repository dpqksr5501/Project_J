#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"
#include "Misc/ScopeExit.h"
#include "Animation/Project_JGuidedHandIKTrace.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGuidedIKTraceTest,
	"ProjectJ.Animation.GuidedIKTraceIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJGuidedIKTraceTest::RunTest(const FString&)
{
	IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Animation.GuidedIKTrace"));
	IConsoleVariable* Filter = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Animation.GuidedIKTraceActor"));
	if (!TestNotNull(TEXT("Development trace switch exists"), Enabled) || !TestNotNull(TEXT("Actor filter exists"), Filter)) { return false; }
	const int32 PreviousEnabled = Enabled->GetInt();
	const FString PreviousFilter = Filter->GetString();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		Enabled->Set(PreviousEnabled, ECVF_SetByCode);
		Filter->Set(*PreviousFilter, ECVF_SetByCode);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	ACharacter* Character = World->SpawnActor<ACharacter>();
	USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (!TestNotNull(TEXT("Mesh fixture"), MeshAsset) || !TestNotNull(TEXT("Character fixture"), Character)) { return false; }
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->SetSkeletalMeshAsset(MeshAsset);
	UAnimInstance* Instance = NewObject<UAnimInstance>(Mesh);
	TArray<FBoneIndexType> RequiredIndices;
	for (int32 I = 0; I < MeshAsset->GetRefSkeleton().GetNum(); ++I) { RequiredIndices.Add(I); }
	FBoneContainer Bones;
	Bones.InitializeTo(RequiredIndices, UE::Anim::FCurveFilterSettings(), *MeshAsset);
	FMemMark Mark(FMemStack::Get());
	FCompactPose InputPose;
	InputPose.SetBoneContainer(&Bones);
	InputPose.ResetToRefPose();
	FAnimInstanceProxy Proxy(Instance);
	FComponentSpacePoseContext Output(&Proxy);
	Output.Pose.InitPose(MoveTemp(InputPose));
	const FCompactPoseBoneIndex Hand = Bones.GetCompactPoseIndexFromSkeletonIndex(MeshAsset->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TEXT("hand_r")));
	if (!TestTrue(TEXT("Fixture hand exists in compact pose"), Hand != INDEX_NONE)) { return false; }
	const FCompactPoseBoneIndex Forearm = Bones.GetParentBoneIndex(Hand);
	if (!TestTrue(TEXT("Fixture forearm exists in compact pose"), Forearm != INDEX_NONE)) { return false; }
	const FCompactPoseBoneIndex Upper = Bones.GetParentBoneIndex(Forearm);
	if (!TestTrue(TEXT("Fixture upper arm exists in compact pose"), Upper != INDEX_NONE)) { return false; }
	const FTransform Input[] = { Output.Pose.GetComponentSpaceTransform(Upper),
		Output.Pose.GetComponentSpaceTransform(Forearm), Output.Pose.GetComponentSpaceTransform(Hand) };
	FTransform Target = Input[2];
	Target.AddToTranslation(FVector(5.0, 4.0, 2.0));
	FTransform Solved[3];
	Project_J::Animation::FGuidedArmSolveDiagnostics Diagnostics;
	if (!TestTrue(TEXT("Trace solve fixture"), Project_J::Animation::SolveGuidedArm(Input[0], Input[1], Input[2], Target,
		FVector::YAxisVector, nullptr, true, Solved[0], Solved[1], Solved[2], &Diagnostics))) { return false; }
	TArray<FBoneTransform> Transforms = { FBoneTransform(Upper, Solved[0]), FBoneTransform(Forearm, Solved[1]), FBoneTransform(Hand, Solved[2]) };
	TSharedPtr<FProject_JGuidedHandIKTraceState> State;
	Enabled->Set(1, ECVF_SetByCode);
	Filter->Set(TEXT(""), ECVF_SetByCode);
	SnapshotGuidedIKTrace(State, &State, Instance, TEXT("hand_r"));
	TestTrue(TEXT("Enabled trace captures the first evaluation"), ShouldCaptureGuidedIKTrace(State));
	if (State)
	{
		LogGuidedIKTrace(*State, Output, Input, Solved, Target, 0.4f, 0.4f, TEXT("IsolationTest"), &Transforms, &Diagnostics);
		TestTrue(TEXT("Debug alpha probe leaves the live input pose unchanged"),
			Output.Pose.GetComponentSpaceTransform(Upper).Equals(Input[0], 0.000001f) &&
			Output.Pose.GetComponentSpaceTransform(Forearm).Equals(Input[1], 0.000001f) &&
			Output.Pose.GetComponentSpaceTransform(Hand).Equals(Input[2], 0.000001f));
		TestFalse(TEXT("Only one sample is logged for a pre-update epoch"), ShouldCaptureGuidedIKTrace(State));
	}
	Enabled->Set(0, ECVF_SetByCode);
	SnapshotGuidedIKTrace(State, &State, Instance, TEXT("hand_r"));
	TestFalse(TEXT("Disabled trace does not capture"), ShouldCaptureGuidedIKTrace(State));
	State.Reset();
	Filter->Set(TEXT("ActorNotInThisFixture"), ECVF_SetByCode);
	Enabled->Set(1, ECVF_SetByCode);
	SnapshotGuidedIKTrace(State, &State, Instance, TEXT("hand_r"));
	TestFalse(TEXT("Actor filter excludes unrelated actors"), ShouldCaptureGuidedIKTrace(State));
	return true;
}
#endif
