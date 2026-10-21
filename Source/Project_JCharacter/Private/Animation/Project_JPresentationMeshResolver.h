#pragma once

#include "CoreMinimal.h"

class APawn;
class USkeletalMesh;
class USkeletalMeshComponent;

namespace Project_J::Animation
{
    /** Cloth has a separate engine tick, independent of anim graph cadence. */
    bool UsesIndependentClothSimulation(const USkeletalMeshComponent& Mesh);
	/** Visual demand only. Gameplay pose/authority demand is evaluated separately. */
	bool WasCharacterVisualRecentlyRendered(const APawn& Character, float Tolerance);
	/** Unique explicit role, otherwise a unique native main/PP retargeter. Ambiguity never picks an arbitrary garment. */
	USkeletalMeshComponent* FindVisualFollower(const APawn& Character, bool bRequireMeshAsset = true);

	/** A modular part may copy bones only from a mesh using its actual skeleton. */
	USkeletalMeshComponent* ResolveEquipmentPoseSource(const APawn& Character, const USkeletalMesh& EquipmentMesh, bool bCopyPose = false);

	/** Prefer a follower socket for cosmetics; preserve the source socket as a safe legacy fallback. */
	USkeletalMeshComponent* ResolveWeaponAttachmentMesh(const APawn& Character, FName SourceSocket,
		FName VisualSocket, bool bPreferVisualFollower, FName& OutSocket);
}
