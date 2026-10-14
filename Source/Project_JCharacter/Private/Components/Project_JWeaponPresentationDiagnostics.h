#pragma once

#include "CoreMinimal.h"

class USceneComponent;
DECLARE_LOG_CATEGORY_EXTERN(LogProjectJWeaponPresentation, Log, All);

namespace Project_J::WeaponPresentation
{
bool IsDebugEnabled();
bool IsGripTraceEnabled();
// Runtime calibration and diagnostics share this rigid authored-space resolver.
bool ResolveRigidGripInRoot(const USceneComponent* GripComponent, FName GripName,
	const USceneComponent* Root, FTransform& OutGrip, FName& OutReason);
}
