#include "Animation/Project_JHandContact.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Project_JPlayerCharacter.h"

namespace
{
bool IsInvertible(const FTransform& Transform)
{
	return !Transform.ContainsNaN() && Transform.GetScale3D().GetAbs().GetMin() > UE_KINDA_SMALL_NUMBER;
}
}

FProject_JHandGripCalibration Project_J::Animation::ResolveHandGripCalibration(
	const ACharacter* Character, const UProject_JHandGripProfile* VisualOverride)
{
	check(IsInGameThread());
	if (VisualOverride) { return VisualOverride->Calibration; }
	const AProject_JPlayerCharacter* Player = Cast<AProject_JPlayerCharacter>(Character);
	const UProject_JCharacterAnimProfile* Profile = Player ? Player->GetCharacterAnimProfile() : nullptr;
	return Profile ? (Profile->HandGripProfile ? Profile->HandGripProfile->Calibration : Profile->HandGripCalibration)
		: FProject_JHandGripCalibration();
}

Project_J::Animation::FResolvedHandContact Project_J::Animation::ResolveHandContact(
	const USkeletalMeshComponent& Mesh, FName PalmSocket, FName HandBone, bool bAllowLegacyWrist)
{
	check(IsInGameThread());
	FResolvedHandContact Result;
	const USkeletalMeshSocket* Socket = PalmSocket.IsNone() ? nullptr : Mesh.GetSocketByName(PalmSocket);
	if (!Socket)
	{
		Result.Hand = HandBone;
		Result.Status = bAllowLegacyWrist ? EHandContactStatus::LegacyWrist : EHandContactStatus::MissingSocket;
		// Legacy conversion is identity and may have no named hand. The old graph
		// still owns its tip bone. Explicit invalid bone names are never accepted.
		if (!HandBone.IsNone() && Mesh.GetBoneIndex(HandBone) == INDEX_NONE) { Result.Status = EHandContactStatus::InvalidHand; }
		return Result;
	}
	Result.Hand = HandBone.IsNone() ? Socket->BoneName : HandBone;
	if (Result.Hand != Socket->BoneName || Mesh.GetBoneIndex(Result.Hand) == INDEX_NONE)
	{
		Result.Status = EHandContactStatus::InvalidHand;
		return Result;
	}
	// Mesh-specific sockets take precedence through the component's lookup.
	// Read the authored local anchor, not last frame's final IK hand transform.
	Result.PalmInHand = Socket->GetSocketLocalTransform();
	Result.Status = IsInvertible(Result.PalmInHand) ? EHandContactStatus::Socket : EHandContactStatus::InvalidOffset;
	return Result;
}

bool Project_J::Animation::MakeWristContactTarget(const FTransform& ContactGoal,
	const FTransform& PalmInHand, const FTransform& BodyContactOffset, FTransform& OutWrist, const FVector& WristScale)
{
	OutWrist = FTransform::Identity;
	if (!IsInvertible(ContactGoal) || !IsInvertible(PalmInHand) || !IsInvertible(BodyContactOffset) ||
		WristScale.ContainsNaN() || WristScale.GetAbs().GetMin() <= UE_KINDA_SMALL_NUMBER) { return false; }
	const FTransform Target = BodyContactOffset * ContactGoal;
	// Marker/weapon scale cannot resize a character's hand. Preserve anatomy scale
	// while matching the Palm contact position and orientation.
	const FQuat Rotation = (Target.GetRotation() * PalmInHand.GetRotation().Inverse()).GetNormalized();
	OutWrist = FTransform(Rotation, Target.GetLocation() - Rotation.RotateVector(WristScale * PalmInHand.GetLocation()), WristScale);
	return !OutWrist.ContainsNaN();
}

bool Project_J::Animation::IsPositiveUniformContactScale(const FVector& Scale)
{
	if (Scale.ContainsNaN() || Scale.GetMin() <= UE_KINDA_SMALL_NUMBER) { return false; }
	// Evaluated float bone scales are promoted to double by FVector. The default
	// double NearlyEqual tolerance incorrectly treats harmless pose roundoff as shear.
	return Scale.GetMax() - Scale.GetMin() <= Scale.GetMax() * 1.e-4;
}

bool Project_J::Animation::MakePrimaryGripAttachment(const FTransform& GripInRoot,
	const FTransform& PalmInHand, const FTransform& BodyContactOffset, FTransform& OutRootInHand)
{
	OutRootInHand = FTransform::Identity;
	for (const FTransform* Value : { &GripInRoot, &PalmInHand, &BodyContactOffset })
	{
		if (!IsInvertible(*Value) || !IsPositiveUniformContactScale(Value->GetScale3D())) { return false; }
	}
	const FTransform ContactInRoot = BodyContactOffset * GripInRoot;
	// Align position and orientation, not marker scale. Inverting the entire
	// contact transform would resize the weapon to cancel its authored child scale.
	const FQuat Rotation = (PalmInHand.GetRotation() * ContactInRoot.GetRotation().Inverse()).GetNormalized();
	OutRootInHand = FTransform(Rotation,
		PalmInHand.GetLocation() - Rotation.RotateVector(ContactInRoot.GetLocation()), FVector::OneVector);
	return !OutRootInHand.ContainsNaN();
}
