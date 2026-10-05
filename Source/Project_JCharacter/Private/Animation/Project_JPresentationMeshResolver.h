#pragma once

#include "CoreMinimal.h"

class ACharacter;
class USkeletalMesh;
class USkeletalMeshComponent;

namespace Project_J::Animation
{
	/** Visual demand only. Gameplay pose/authority demand is evaluated separately. */
	bool WasCharacterVisualRecentlyRendered(const ACharacter& Character, float Tolerance);
	/** The visible retarget follower, if this character uses a separate animation source mesh. */
	USkeletalMeshComponent* FindVisualFollower(const ACharacter& Character);

	/** A modular part may copy bones only from a mesh using its actual skeleton. */
	USkeletalMeshComponent* ResolveEquipmentPoseSource(const ACharacter& Character, const USkeletalMesh& EquipmentMesh);

	/** Prefer a follower socket for cosmetics; preserve the source socket as a safe legacy fallback. */
	USkeletalMeshComponent* ResolveWeaponAttachmentMesh(const ACharacter& Character, FName SourceSocket,
		FName VisualSocket, bool bPreferVisualFollower, FName& OutSocket);
}
