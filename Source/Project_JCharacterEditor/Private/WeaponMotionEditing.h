#pragma once
#include "CoreMinimal.h"

namespace Project_J::WeaponMotionEditor
{
	/** Runtime data is socket-relative. Nonuniform/mirrored attachment scale cannot preserve rigid rotation. */
	inline bool ApplyWorldDelta(FTransform& Relative, const FTransform& SocketWorld, const FVector& Drag, const FRotator& Rotation)
	{
		const FVector Scale = SocketWorld.GetScale3D();
		if (Relative.ContainsNaN() || SocketWorld.ContainsNaN() || !Relative.IsRotationNormalized() || !SocketWorld.IsRotationNormalized() || Drag.ContainsNaN() || Rotation.ContainsNaN() ||
			Scale.X <= UE_SMALL_NUMBER || !Scale.Equals(FVector(Scale.X), UE_KINDA_SMALL_NUMBER)) { return false; }
		FTransform Candidate = Relative;
		const FVector Translation = Relative.GetTranslation() + SocketWorld.InverseTransformVector(Drag);
		if (Translation.ContainsNaN()) return false;
		Candidate.SetTranslation(Translation);
		FVector Axis; double Angle;
		Rotation.Quaternion().ToAxisAndAngle(Axis, Angle);
		if (!Axis.IsNearlyZero() && !FMath::IsNearlyZero(Angle))
		{
			Candidate.SetRotation(FQuat(SocketWorld.InverseTransformVectorNoScale(Axis).GetSafeNormal(), Angle) * Candidate.GetRotation());
			Candidate.NormalizeRotation();
		}
		if (Candidate.ContainsNaN() || !Candidate.IsRotationNormalized()) return false;
		Relative = Candidate;
		return true;
	}
}
