#include "Animation/Project_JHandContact.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMeshSocket.h"

namespace
{
bool IsInvertible(const FTransform& Transform)
{
	return !Transform.ContainsNaN() && Transform.GetScale3D().GetAbs().GetMin() > UE_KINDA_SMALL_NUMBER;
}
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
	const FTransform& PalmInHand, const FTransform& BodyContactOffset, FTransform& OutWrist)
{
	OutWrist = FTransform::Identity;
	if (!IsInvertible(ContactGoal) || !IsInvertible(PalmInHand) || !IsInvertible(BodyContactOffset)) { return false; }
	OutWrist = PalmInHand.Inverse() * (BodyContactOffset * ContactGoal);
	OutWrist.NormalizeRotation();
	return !OutWrist.ContainsNaN();
}
