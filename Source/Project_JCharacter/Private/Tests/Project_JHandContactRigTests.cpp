#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JGuidedArmChain.h"
#include "Animation/Project_JGuidedArmSolver.h"
#include "Animation/Project_JAnimNode_GuidedHandIK.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ReferenceSkeleton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandContactMathTest,
	"ProjectJ.Animation.HandContactConversion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandContactMathTest::RunTest(const FString&)
{
	using namespace Project_J::Animation;
	const FTransform Goal(FRotator(24.0, -55.0, 13.0), FVector(140.0, -72.0, 31.0));
	const FTransform Offset(FRotator(0.0, 14.0, -8.0), FVector(2.0, -3.0, 1.0));
	for (const FVector& PalmLocation : { FVector(6.0, 2.0, -1.0), FVector(11.0, -4.0, 2.0), FVector(-5.0, 3.0, 4.0) })
	{
		const FTransform PalmInHand(FRotator(18.0, -30.0, 72.0), PalmLocation);
		FTransform Wrist;
		TestTrue(TEXT("An arbitrary body palm anchor converts to a wrist goal"), MakeWristContactTarget(Goal, PalmInHand, Offset, Wrist));
		TestTrue(TEXT("The solved wrist places the palm at the desired contact position AND orientation"),
			(PalmInHand * Wrist).Equals(Offset * Goal, 0.0001f));
		const FTransform Component(FRotator(-12.0, 60.0, 10.0), FVector(-300.0, 200.0, 72.0), FVector(1.75));
		const FTransform WristCS = Wrist.GetRelativeTransform(Component);
		TestTrue(TEXT("A rotated, translated and uniformly scaled mesh preserves world contact"),
			(PalmInHand * (WristCS * Component)).Equals(Offset * Goal, 0.0001f));
		// Same conversion works in primary-hand space, with no special solver or job branch.
		const FTransform HandSpace(FRotator(4.0, 25.0, 9.0), FVector(3.0, 14.0, -2.0));
		TestTrue(TEXT("Secondary primary-hand-space contact uses the same conversion"),
			MakeWristContactTarget(HandSpace, PalmInHand, Offset, Wrist) && (PalmInHand * Wrist).Equals(Offset * HandSpace, 0.0001f));
	}
	FTransform Wrist;
	TestTrue(TEXT("Legacy wrist origin retains existing target values"),
		MakeWristContactTarget(Goal, FTransform::Identity, FTransform::Identity, Wrist) && Wrist.Equals(Goal));
	FTransform Singular = FTransform::Identity;
	Singular.SetScale3D(FVector(0.0, 1.0, 1.0));
	TestFalse(TEXT("A singular palm anchor cannot fabricate an inverse"), MakeWristContactTarget(Goal, Singular, Offset, Wrist));
	TestTrue(TEXT("Failed conversion clears its output"), Wrist.Equals(FTransform::Identity));
	const FTransform Palm(FRotator(15, 27, -35), FVector(8, -3, 2));
	const FTransform Child(FRotator(-12, 42, 18), FVector(10, 4, -6), FVector(2));
	const FTransform Grip(FRotator(8, -21, 11), FVector(3, 6, 9));
	FTransform Mount;
	TestTrue(TEXT("A rotated/scaled child mesh produces a fixed root-in-hand mount"),
		MakePrimaryGripAttachment(Grip * Child, Palm, Offset, Mount));
	TestTrue(TEXT("Mounting preserves child mesh size instead of shrinking the weapon"), Mount.GetScale3D().Equals(FVector::OneVector));
	const FTransform Hand(FRotator(16, -34, 42), FVector(400, 300, -20), FVector(1.75));
	const FTransform WeaponGoal = Grip * Child * (Mount * Hand);
	const FTransform ActualPalm = Palm * Hand;
	const FTransform CalibratedGoal = Offset * WeaponGoal;
	TestTrue(TEXT("Idle contact matches Palm position/orientation with body AND weapon scaling"),
		ActualPalm.GetLocation().Equals(CalibratedGoal.GetLocation(), 0.0001) &&
		ActualPalm.GetRotation().Equals(CalibratedGoal.GetRotation(), 0.0001));
	TestTrue(TEXT("Attack contact reconstructs the same wrist without weapon scale leaking into anatomy"),
		MakeWristContactTarget(WeaponGoal, Palm, Offset, Wrist, Hand.GetScale3D()) && Wrist.Equals(Hand, 0.0001));
	FTransform Unsupported = Child;
	Unsupported.SetScale3D(FVector(2, 2.000004, 1.999998));
	TestTrue(TEXT("Evaluated float scale roundoff is accepted by contact calibration"),
		MakePrimaryGripAttachment(Unsupported, Palm, Offset, Mount));
	TestFalse(TEXT("Material anisotropy remains unsupported"), IsPositiveUniformContactScale(FVector(1, 1.001, 1)));
	TestFalse(TEXT("Zero scale remains unsupported"), IsPositiveUniformContactScale(FVector::ZeroVector));
	Unsupported.SetScale3D(FVector(1, 2, 1));
	TestFalse(TEXT("A shear-producing mount is rejected instead of silently misaligning contact"),
		MakePrimaryGripAttachment(Unsupported, Palm, Offset, Mount));
	Unsupported.SetScale3D(FVector(-1));
	TestFalse(TEXT("Mirrored mount requires explicit custom ownership"), MakePrimaryGripAttachment(Unsupported, Palm, Offset, Mount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandContactRigTest,
	"ProjectJ.Animation.HandContactRig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHandContactRigTest::RunTest(const FString&)
{
	using namespace Project_J::Animation;
	USkeleton* Skeleton = NewObject<USkeleton>();
	const FName Names[] = { TEXT("root"), TEXT("arm_anchor"), TEXT("upper_helper"), TEXT("hinge"), TEXT("lower_helper"), TEXT("wrist_end"), TEXT("finger"), TEXT("other_wrist") };
	const int32 Parents[] = { INDEX_NONE, 0, 1, 2, 3, 4, 5, 0 };
	const FVector Local[] = { FVector::ZeroVector, FVector::ZeroVector, FVector(15,10,0), FVector(15,10,0), FVector(15,-10,0), FVector(15,-10,0), FVector(4,0,0), FVector(0,-30,0) };
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			Modifier.Add(FMeshBoneInfo(Names[I], Names[I].ToString(), Parents[I]), FTransform(Local[I]));
		}
	}
	USkeletalMesh* MeshAsset = NewObject<USkeletalMesh>();
	MeshAsset->SetSkeleton(Skeleton);
	MeshAsset->GetRefSkeleton() = Skeleton->GetReferenceSkeleton();
	USkeletalMeshSocket* SkeletonPalm = NewObject<USkeletalMeshSocket>(Skeleton);
	SkeletonPalm->SocketName = TEXT("PalmAnchor");
	SkeletonPalm->BoneName = Names[5];
	SkeletonPalm->RelativeLocation = FVector(99,0,0);
	Skeleton->Sockets.Add(SkeletonPalm);
	USkeletalMeshSocket* MeshPalm = NewObject<USkeletalMeshSocket>(MeshAsset);
	MeshPalm->SocketName = SkeletonPalm->SocketName;
	MeshPalm->BoneName = Names[5];
	MeshPalm->RelativeLocation = FVector(6,2,-1);
	MeshPalm->RelativeRotation = FRotator(18,-30,72);
	MeshAsset->GetMeshOnlySocketList().Add(MeshPalm);
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	Mesh->SetSkeletalMeshAsset(MeshAsset);
	const FResolvedHandContact Anchor = ResolveHandContact(*Mesh, MeshPalm->SocketName, NAME_None, false);
	TestTrue(TEXT("A mesh-specific anchor overrides the shared skeleton socket and infers the hand"),
		Anchor.IsValid() && Anchor.Hand == Names[5] && Anchor.PalmInHand.Equals(MeshPalm->GetSocketLocalTransform()));
	TestFalse(TEXT("A profile naming a different hand cannot silently align another bone"),
		ResolveHandContact(*Mesh, MeshPalm->SocketName, Names[7], true).IsValid());
	TestFalse(TEXT("Strict missing-socket policy disables contact"), ResolveHandContact(*Mesh, TEXT("missing"), Names[5], false).IsValid());
	TestTrue(TEXT("Legacy missing-socket policy is explicitly compatible"),
		ResolveHandContact(*Mesh, TEXT("missing"), Names[5], true).Status == EHandContactStatus::LegacyWrist);
	TestFalse(TEXT("Legacy policy never accepts an explicitly invalid hand name"),
		ResolveHandContact(*Mesh, TEXT("missing"), TEXT("not_a_bone"), true).IsValid());

	TArray<FBoneIndexType> Required;
	for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I) { Required.Add(I); }
	FBoneContainer Bones;
	Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(), *Skeleton);
	FGuidedArmChain Chain;
	TestTrue(TEXT("Explicit anatomical roles resolve across upper/lower helper bones"),
		ResolveGuidedArmChain(Bones, Names[5], Names[3], Names[1], Chain) && Chain.Path.Num() == 5);
	FGuidedArmChain Bad;
	TestFalse(TEXT("A forearm on another branch is rejected"), ResolveGuidedArmChain(Bones, Names[5], Names[7], Names[1], Bad));
	TestFalse(TEXT("Partial explicit roles are rejected instead of guessed"), ResolveGuidedArmChain(Bones, Names[5], Names[3], NAME_None, Bad));
	TestTrue(TEXT("Legacy immediate-parent mode remains available"), ResolveGuidedArmChain(Bones, Names[5], NAME_None, NAME_None, Bad) && Bad.Path.Num() == 3);
	FMemMark Mark(FMemStack::Get());
	FCompactPose Compact;
	Compact.SetBoneContainer(&Bones);
	Compact.ResetToRefPose();
	UProject_JRetargetAnimInstance* Instance = NewObject<UProject_JRetargetAnimInstance>(Mesh);
	FAnimInstanceProxy Proxy(Instance);
	FComponentSpacePoseContext Output(&Proxy);
	Output.Pose.InitPose(MoveTemp(Compact));
	const FTransform Input[] = { Output.Pose.GetComponentSpaceTransform(Chain.Upper),
		Output.Pose.GetComponentSpaceTransform(Chain.Forearm), Output.Pose.GetComponentSpaceTransform(Chain.Hand) };
	const FTransform Target(FRotator(15,35,70), FVector(45,20,12));
	FTransform Solved[3];
	TestTrue(TEXT("An explicit helper chain uses actual anatomical segment lengths"),
		SolveGuidedArm(Input[0], Input[1], Input[2], Target, FVector::YAxisVector, nullptr, true, Solved[0], Solved[1], Solved[2]));
	TArray<FBoneTransform> Corrections;
	TestTrue(TEXT("All intervening helpers receive coherent segment-relative corrections"),
		AppendGuidedArmTransforms(Chain, Output.Pose, Input, Solved, Corrections) && Corrections.Num() == 5);
	for (int32 I : { 1, 3 })
	{
		const int32 Segment = I == 1 ? 0 : 1;
		TestTrue(TEXT("Helper local position/rotation/scale relative to its segment root is preserved"),
			Corrections[I].Transform.GetRelativeTransform(Solved[Segment]).Equals(
				Output.Pose.GetComponentSpaceTransform(Chain.Path[I]).GetRelativeTransform(Input[Segment]), 0.0001f));
	}
	FComponentSpacePoseContext Probe(Output);
	Probe.Pose.CopyPose(Output.Pose);
	Probe.Pose.LocalBlendCSBoneTransforms(Corrections, 1.0f);
	TestTrue(TEXT("The engine's full-alpha hierarchy blend preserves exact wrist contact"),
		Probe.Pose.GetComponentSpaceTransform(Chain.Hand).Equals(Target, 0.001f));
	TestTrue(TEXT("The input pose remains unchanged by solving and constructing corrections"),
		Output.Pose.GetComponentSpaceTransform(Chain.Hand).Equals(Input[2]));
	UProject_JHandGripProfile* Profile = NewObject<UProject_JHandGripProfile>();
	Profile->Calibration.PrimaryPalmSocketName = MeshPalm->SocketName;
	Profile->Calibration.PrimaryArm.Hand = Names[5];
	Profile->Calibration.PrimaryArm.Elbow = Names[3];
	Profile->Calibration.PrimaryArm.Shoulder = Names[1];
	Profile->Calibration.MissingPalmPolicy = EProject_JMissingPalmPolicy::DisableContactIK;
	Instance->HandGripProfile = Profile;
	FProject_JAnimNode_GuidedHandIK Node;
	Node.ArmDefinitionSource = EProject_JGuidedArmDefinitionSource::PrimaryBodyProfile;
	Node.EffectorTransform = Target;
	Node.PreUpdate(Instance);
	Node.InitializeBoneReferences(Bones);
	TestTrue(TEXT("The graph node reads an arbitrary body profile without mannequin/job names"), Node.IsValidToEvaluate(Skeleton, Bones));
	Node.SetAlpha(1.0f);
	Corrections.Reset();
	Node.EvaluateSkeletalControl_AnyThread(Output, Corrections);
	TestTrue(TEXT("The profile-driven graph node emits the helper chain and exact wrist goal"),
		Corrections.Num() == 5 && Corrections.Last().Transform.Equals(Target, 0.001f));
	Profile->Calibration.PrimaryPalmSocketName = TEXT("missing");
	Node.PreUpdate(Instance);
	TestFalse(TEXT("A live profile/socket change invalidates the chain, with no stale contact"), Node.IsValidToEvaluate(Skeleton, Bones));
	Profile->Calibration.PrimaryPalmSocketName = MeshPalm->SocketName;
	Node.PreUpdate(Instance);
	TestTrue(TEXT("Restoring a valid anchor recovers the chain"), Node.IsValidToEvaluate(Skeleton, Bones));
	TArray<FBoneIndexType> Reduced = { 0,1,2,3,4 };
	FBoneContainer LODBones;
	LODBones.InitializeTo(Reduced, UE::Anim::FCurveFilterSettings(), *Skeleton);
	Node.InitializeBoneReferences(LODBones);
	TestFalse(TEXT("LOD removal of the endpoint bypasses the contact solve"), Node.IsValidToEvaluate(Skeleton, LODBones));
	return true;
}
#endif
