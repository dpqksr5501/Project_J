#pragma once
#include "CoreMinimal.h"

namespace Project_J::WeaponMotionEditor
{
	/** Runtime data is socket-relative. Nonuniform/mirrored attachment scale cannot preserve rigid rotation. */
	inline bool ApplyWorldDelta(FTransform& Relative, const FTransform& SocketWorld, const FVector& Drag, const FRotator& Rotation)
	{
		const FVector Scale = SocketWorld.GetScale3D();
		if (Relative.ContainsNaN() || SocketWorld.ContainsNaN() || Drag.ContainsNaN() || Rotation.ContainsNaN() ||
			Scale.X <= UE_SMALL_NUMBER || !Scale.Equals(FVector(Scale.X), UE_KINDA_SMALL_NUMBER)) { return false; }
		Relative.AddToTranslation(SocketWorld.InverseTransformVector(Drag));
		FVector Axis; double Angle;
		Rotation.Quaternion().ToAxisAndAngle(Axis, Angle);
		if (!Axis.IsNearlyZero() && !FMath::IsNearlyZero(Angle))
		{
			Relative.SetRotation(FQuat(SocketWorld.InverseTransformVectorNoScale(Axis).GetSafeNormal(), Angle) * Relative.GetRotation());
			Relative.NormalizeRotation();
		}
		return true;
	}
}
