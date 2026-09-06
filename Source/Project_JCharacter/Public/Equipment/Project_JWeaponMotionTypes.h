#pragma once

#include "CoreMinimal.h"
#include "Project_JWeaponMotionTypes.generated.h"

/**
 * One authored key for a weapon's independent motion window.
 *
 * This data is embedded directly in the Weapon Motion notify state on its
 * Montage. Time is normalized to that notify window, and the transform is
 * relative to the weapon root at its drawn character socket. Identity is the
 * ordinary socket-attached pose; no character skeleton extension is needed.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JWeaponMotionKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalizedTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Motion")
	FTransform RelativeTransform = FTransform::Identity;
};

namespace Project_J::WeaponMotion
{
	/** Evaluates the compact key list used by both runtime and editor preview. */
	inline FTransform EvaluateKeys(const TArray<FProject_JWeaponMotionKey>& Keys, float NormalizedTime)
	{
		if (Keys.IsEmpty())
		{
			return FTransform::Identity;
		}

		const float Time = FMath::Clamp(NormalizedTime, 0.0f, 1.0f);
		if (Time <= Keys[0].NormalizedTime)
		{
			return Keys[0].RelativeTransform;
		}

		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			const FProject_JWeaponMotionKey& RightKey = Keys[Index];
			if (Time <= RightKey.NormalizedTime)
			{
				const FProject_JWeaponMotionKey& LeftKey = Keys[Index - 1];
				const float Span = FMath::Max(RightKey.NormalizedTime - LeftKey.NormalizedTime, UE_KINDA_SMALL_NUMBER);
				FTransform Result;
				Result.Blend(LeftKey.RelativeTransform, RightKey.RelativeTransform, FMath::Clamp((Time - LeftKey.NormalizedTime) / Span, 0.0f, 1.0f));
				return Result;
			}
		}

		return Keys.Last().RelativeTransform;
	}

	/**
	 * Fades the whole authored timeline from/to the normal drawn-socket pose.
	 * This is a state-boundary hand-off, not another authored transform key.
	 */
	inline float EvaluateStateBlendAlpha(float NormalizedTime, float StateDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
	{
		const float Time = FMath::Clamp(NormalizedTime, 0.0f, 1.0f) * FMath::Max(StateDurationSeconds, 0.0f);
		const float RemainingTime = FMath::Max(StateDurationSeconds - Time, 0.0f);
		float Alpha = 1.0f;
		if (EntryBlendSeconds > UE_KINDA_SMALL_NUMBER)
		{
			Alpha = FMath::Min(Alpha, FMath::Clamp(Time / EntryBlendSeconds, 0.0f, 1.0f));
		}
		if (ExitBlendSeconds > UE_KINDA_SMALL_NUMBER)
		{
			Alpha = FMath::Min(Alpha, FMath::Clamp(RemainingTime / ExitBlendSeconds, 0.0f, 1.0f));
		}
		return Alpha;
	}

	inline FTransform EvaluateStateTransform(const TArray<FProject_JWeaponMotionKey>& Keys, float NormalizedTime, float StateDurationSeconds, float EntryBlendSeconds, float ExitBlendSeconds)
	{
		FTransform Result;
		Result.Blend(FTransform::Identity, EvaluateKeys(Keys, NormalizedTime), EvaluateStateBlendAlpha(NormalizedTime, StateDurationSeconds, EntryBlendSeconds, ExitBlendSeconds));
		return Result;
	}
}
