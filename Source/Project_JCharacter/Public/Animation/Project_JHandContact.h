#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
class ACharacter;
class UProject_JHandGripProfile;
struct FProject_JHandGripCalibration;
struct FProject_JGripArmBones;

namespace Project_J::Animation
{
	/** One precedence rule for visual animation and normal weapon mounting. Game thread only. */
	PROJECT_JCHARACTER_API FProject_JHandGripCalibration ResolveHandGripCalibration(
		const ACharacter* Character, const UProject_JHandGripProfile* VisualOverride);
	enum class EHandContactStatus : uint8 { Socket, LegacyWrist, MissingSocket, InvalidHand, InvalidOffset };
	/** Game-thread resolved body data. No UObject pointer or world-space pose is carried to a solver. */
	struct FResolvedHandContact
	{
		FName Hand = NAME_None;
		FTransform PalmInHand = FTransform::Identity;
		EHandContactStatus Status = EHandContactStatus::MissingSocket;
		bool IsValid() const { return Status == EHandContactStatus::Socket || Status == EHandContactStatus::LegacyWrist; }
	};

	/** A palm anchor must be on the configured hand bone; moving finger anchors are not fixed calibration. */
	PROJECT_JCHARACTER_API FResolvedHandContact ResolveHandContact(const USkeletalMeshComponent& Mesh,
		FName PalmSocket, FName HandBone, bool bAllowLegacyWrist);

	/** Value-only conversion, usable with Guided IK, FABRIK, Two Bone IK or a Control Rig. */
	PROJECT_JCHARACTER_API bool MakeWristContactTarget(const FTransform& ContactGoal,
		const FTransform& PalmInHand, const FTransform& BodyContactOffset, FTransform& OutWrist,
		const FVector& WristScale = FVector::OneVector);

	/** Accept float pose/import roundoff, but reject material anisotropy, singular and mirrored scales. */
	PROJECT_JCHARACTER_API bool IsPositiveUniformContactScale(const FVector& Scale);

	/** Fixed root-in-hand mount, preserving weapon mesh size. Positive uniform scales only (no shear/mirroring). */
	PROJECT_JCHARACTER_API bool MakePrimaryGripAttachment(const FTransform& GripInRoot,
		const FTransform& PalmInHand, const FTransform& BodyContactOffset, FTransform& OutRootInHand);
}
