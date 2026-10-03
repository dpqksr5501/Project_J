#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"
#include "Animation/Project_JAnimNode_GuidedHandIK.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ReferenceSkeleton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSecondaryHandContactTest,
	"ProjectJ.Animation.SecondaryHandContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSecondaryHandContactTest::RunTest(const FString&)
{
	// Arbitrary names, asymmetric segment lengths, no dedicated IK bones.
	USkeleton* Skeleton = NewObject<USkeleton>();
	const FName Names[] = { TEXT("root"), TEXT("driver_anchor"), TEXT("driver_hinge"), TEXT("driver_contact"),
		TEXT("support_anchor"), TEXT("support_hinge"), TEXT("support_contact"), TEXT("support_finger") };
	const int32 Parents[] = { INDEX_NONE, 0, 1, 2, 0, 4, 5, 6 };
	const FVector Locations[] = { FVector::ZeroVector, FVector(0,6,0), FVector(22,9,0), FVector(26,-9,0),
		FVector(0,-6,0), FVector(24,-8,0), FVector(27,8,0), FVector(3,0,0) };
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			Modifier.Add(FMeshBoneInfo(Names[I], Names[I].ToString(), Parents[I]), FTransform(Locations[I]));
		}
	}
	USkeletalMesh* MeshAsset = NewObject<USkeletalMesh>();
	MeshAsset->SetSkeleton(Skeleton);
	MeshAsset->GetRefSkeleton() = Skeleton->GetReferenceSkeleton();
	USkeletalMeshSocket* Palm = NewObject<USkeletalMeshSocket>(MeshAsset);
	Palm->SocketName = TEXT("SupportPalm"); Palm->BoneName = Names[6];
	Palm->RelativeLocation = FVector(6, 2, -1); Palm->RelativeRotation = FRotator(12,-8,21);
	MeshAsset->GetMeshOnlySocketList().Add(Palm);
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	Mesh->SetSkeletalMeshAsset(MeshAsset);
	UProject_JRetargetAnimInstance* Instance = NewObject<UProject_JRetargetAnimInstance>(Mesh);
	UProject_JHandGripProfile* Profile = NewObject<UProject_JHandGripProfile>();
	Profile->Calibration.SecondaryPalmSocketName = Palm->SocketName;
	Profile->Calibration.SecondaryArm.Shoulder = Names[4];
	Profile->Calibration.SecondaryArm.Elbow = Names[5];
	Profile->Calibration.SecondaryArm.Hand = Names[6];
	Profile->Calibration.MissingPalmPolicy = EProject_JMissingPalmPolicy::DisableContactIK;
	Instance->HandGripProfile = Profile;
	TArray<FBoneIndexType> Required;
	for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I) { Required.Add(I); }
	FBoneContainer Bones;
	Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *Skeleton);
	FMemMark Mark(FMemStack::Get());
	FCompactPose Compact;
	Compact.SetBoneContainer(&Bones); Compact.ResetToRefPose();
	FAnimInstanceProxy Proxy(Instance);
	FComponentSpacePoseContext Output(&Proxy);
	Output.Pose.InitPose(MoveTemp(Compact));
	const FCompactPoseBoneIndex Driver(3), Support(6);
	const FTransform PreviousDriver = Output.Pose.GetComponentSpaceTransform(Driver);
	FProject_JAnimNode_GuidedHandIK Primary;
	Primary.UpperArmBone.BoneName = Names[1]; Primary.ForearmBone.BoneName = Names[2]; Primary.HandBone.BoneName = Names[3];
	Primary.EffectorTransform = FTransform(FRotator(10,-12,35), FVector(40,5,4));
	Primary.InitializeBoneReferences(Bones);
	TArray<FBoneTransform> Corrections;
	Primary.EvaluateSkeletalControl_AnyThread(Output, Corrections);
	Output.Pose.LocalBlendCSBoneTransforms(Corrections, 0.55f);
	const FTransform CurrentDriver = Output.Pose.GetComponentSpaceTransform(Driver);
	TestFalse(TEXT("Upstream primary solve has moved the current pose away from the previous snapshot"), CurrentDriver.Equals(PreviousDriver));
	const FTransform GripInDriver(FRotator(8,15,-12), FVector(-2,-10,1));
	FTransform WristInDriver;
	TestTrue(TEXT("An off-center support Palm converts once to a reference-bone wrist target"),
		Project_J::Animation::MakeWristContactTarget(GripInDriver, Palm->GetSocketLocalTransform(), FTransform::Identity, WristInDriver));
	FProject_JAnimNode_GuidedHandIK Secondary;
	Secondary.ArmDefinitionSource = EProject_JGuidedArmDefinitionSource::SecondaryBodyProfile;
	Secondary.bUseBoneSpaceEffector = true;
	Secondary.EffectorSpaceBoneName = Names[3]; Secondary.EffectorBoneSpaceTransform = WristInDriver;
	Secondary.PreUpdate(Instance); Secondary.InitializeBoneReferences(Bones);
	TestTrue(TEXT("Secondary profile resolves a separate anatomical chain"), Secondary.IsValidToEvaluate(Skeleton, Bones));
	Corrections.Reset(); Secondary.EvaluateSkeletalControl_AnyThread(Output, Corrections);
	const FTransform Expected = WristInDriver * CurrentDriver;
	TestTrue(TEXT("Secondary target uses this evaluation's alpha-blended primary pose"),
		Corrections.Num() == 3 && Corrections.Last().Transform.Equals(Expected, 0.001));
	if (Corrections.Num() != 3) { return false; }
	TestFalse(TEXT("Secondary cannot lag at the previous primary pose"), Corrections.Last().Transform.Equals(WristInDriver * PreviousDriver, 0.001));
	Output.Pose.LocalBlendCSBoneTransforms(Corrections, 1.0f);
	const FTransform ActualPalm = Palm->GetSocketLocalTransform() * Output.Pose.GetComponentSpaceTransform(Support);
	const FTransform ExpectedPalm = GripInDriver * CurrentDriver;
	TestTrue(TEXT("Full-alpha support contact matches both position and orientation"),
		ActualPalm.GetLocation().Equals(ExpectedPalm.GetLocation(), 0.001) && ActualPalm.GetRotation().Equals(ExpectedPalm.GetRotation(), 0.001));
	TestTrue(TEXT("Secondary solve cannot move the driving hand"), Output.Pose.GetComponentSpaceTransform(Driver).Equals(CurrentDriver, 0.001));
	const FTransform NewDriver(FRotator(-12,18,7), FVector(33,4,7));
	TArray<FBoneTransform> DriverChange = { FBoneTransform(Driver, NewDriver) };
	Output.Pose.LocalBlendCSBoneTransforms(DriverChange, 1.0f);
	Corrections.Reset(); Secondary.EvaluateSkeletalControl_AnyThread(Output, Corrections);
	TestTrue(TEXT("A second evaluation consumes the new pose without a component-world update"),
		Corrections.Num() == 3 && Corrections.Last().Transform.Equals(WristInDriver * NewDriver, 0.001));
	Secondary.bUseBoneSpaceEffector = false;
	Secondary.EffectorTransform = FTransform(FRotator(3,8,19), FVector(38,-8,7));
	TestTrue(TEXT("Independent weapon ownership selects the component-space target"), Secondary.IsValidToEvaluate(Skeleton, Bones));
	Corrections.Reset(); Secondary.EvaluateSkeletalControl_AnyThread(Output, Corrections);
	TestTrue(TEXT("Switching ownership does not reuse the bone-space target"),
		Corrections.Num() == 3 && Corrections.Last().Transform.Equals(Secondary.EffectorTransform, 0.001));
	Secondary.bUseBoneSpaceEffector = true;
	Secondary.EffectorSpaceBoneName = TEXT("missing_driver");
	TestFalse(TEXT("A missing active reference does not fall back to a stale world target"), Secondary.IsValidToEvaluate(Skeleton, Bones));
	Secondary.EffectorSpaceBoneName = Names[7];
	TestFalse(TEXT("A target inside the solved arm subtree is rejected"), Secondary.IsValidToEvaluate(Skeleton, Bones));
	Secondary.EffectorSpaceBoneName = Names[3];
	TestTrue(TEXT("Restoring an independent reference recovers evaluation"), Secondary.IsValidToEvaluate(Skeleton, Bones));
	TArray<FBoneIndexType> Reduced = { 0,4,5,6,7 };
	FBoneContainer LODBones;
	LODBones.InitializeTo(Reduced, UE::Anim::FCurveFilterSettings(), *Skeleton);
	Secondary.InitializeBoneReferences(LODBones);
	TestFalse(TEXT("LOD removal of only the driver bypasses bone-space contact"), Secondary.IsValidToEvaluate(Skeleton, LODBones));
	Secondary.bUseBoneSpaceEffector = false;
	TestTrue(TEXT("Component-space contact does not require the removed driver"), Secondary.IsValidToEvaluate(Skeleton, LODBones));
	return true;
}
#endif
