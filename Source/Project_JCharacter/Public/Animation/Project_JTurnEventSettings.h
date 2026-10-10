#pragma once

#include "CoreMinimal.h"
#include "Project_JTurnEventSettings.generated.h"

/** Forward authored-turn coverage. Normal lifetime is geometry/selection driven;
 * watchdogs only reject an unfinished or stale event. Units are cm/s and degrees. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JTurnEventSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "0"))
	float RunningQualificationSpeed = 180.f;
	/** Commitment. Run Strafe retains its combat profile's authored entry/exit angles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "90", ClampMax = "180"))
	float CommittedTurnAngle = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "0", ClampMax = "180"))
	float ImmediatePathAngle = 135.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "1", ClampMax = "180"))
	float PreparationPathAngle = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "1", ClampMax = "180"))
	float PreparationFacingAngle = 60.f;
	/** Required physical/visual facing still to correct; accumulated mouse yaw alone is insufficient. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Admission", meta = (ClampMin = "1", ClampMax = "180"))
	float MinimumRemainingFacingAngle = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage", meta = (ClampMin = "0", ClampMax = "90"))
	float ForwardConeAngle = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage", meta = (ClampMin = "180", ClampMax = "270"))
	float MaximumRequestedSweep = 225.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "90"))
	float SettledPathAngle = 20.f;
	/** Recovery. Run Strafe uses the combat profile's existing exit angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "45"))
	float CompletionFacingAngle = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "90"))
	float RearmFacingAngle = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "90"))
	float DirectionRetreatAngle = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery", meta = (ClampMin = "0", ClampMax = "180"))
	float ContinuationTargetAngle = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safety", meta = (ClampMin = "0.1", Units = "Seconds"))
	float PreparationWatchdogSeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Safety", meta = (ClampMin = "0.1", Units = "Seconds"))
	float ActiveWatchdogSeconds = .75f;

	FProject_JTurnEventSettings Resolved() const
	{
		FProject_JTurnEventSettings R = *this;
		const FProject_JTurnEventSettings D;
		const auto Safe = [](float V, float F, float L, float H) { return FMath::Clamp(FMath::IsFinite(V) ? V : F, L, H); };
		R.RunningQualificationSpeed = Safe(R.RunningQualificationSpeed, D.RunningQualificationSpeed, 0, 2000);
		R.CommittedTurnAngle = Safe(R.CommittedTurnAngle, D.CommittedTurnAngle, 90, 180);
		R.ImmediatePathAngle = Safe(R.ImmediatePathAngle, D.ImmediatePathAngle, 0, 180);
		R.ForwardConeAngle = Safe(R.ForwardConeAngle, D.ForwardConeAngle, 0, 90);
		R.PreparationPathAngle = Safe(R.PreparationPathAngle, D.PreparationPathAngle, 1, 180);
		R.PreparationFacingAngle = Safe(R.PreparationFacingAngle, D.PreparationFacingAngle, 1, 180);
		R.MinimumRemainingFacingAngle = Safe(R.MinimumRemainingFacingAngle, D.MinimumRemainingFacingAngle, 1, 180);
		R.MaximumRequestedSweep = Safe(R.MaximumRequestedSweep, D.MaximumRequestedSweep, 180, 270);
		R.SettledPathAngle = Safe(R.SettledPathAngle, D.SettledPathAngle, 0, R.PreparationPathAngle - 1.f);
		R.CompletionFacingAngle = Safe(R.CompletionFacingAngle, D.CompletionFacingAngle, 0, FMath::Min(45.f, R.MinimumRemainingFacingAngle - 1.f));
		R.RearmFacingAngle = Safe(R.RearmFacingAngle, D.RearmFacingAngle, 0, 90);
		R.DirectionRetreatAngle = Safe(R.DirectionRetreatAngle, D.DirectionRetreatAngle, 0, 90);
		R.ContinuationTargetAngle = Safe(R.ContinuationTargetAngle, D.ContinuationTargetAngle, 0, 180);
		R.PreparationWatchdogSeconds = Safe(R.PreparationWatchdogSeconds, D.PreparationWatchdogSeconds, .1f, 10);
		R.ActiveWatchdogSeconds = Safe(R.ActiveWatchdogSeconds, D.ActiveWatchdogSeconds, .1f, 5);
		return R;
	}
};
