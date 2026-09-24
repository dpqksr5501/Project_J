#pragma once

#include "CoreMinimal.h"

/** 원격 캐릭터의 이벤트를 재구성한다. 월드와 이동 정보는 컴포넌트가 제공한다. */
class FProject_JRemoteLocomotionRuntime
{
public:
	void Reset() { *this = FProject_JRemoteLocomotionRuntime(); }

	void OnMoveStarted()
	{
		StopStartSuppressTimeRemaining = 0.0f;
		bStopVisualIntentActive = false;
	}

	void OnMoveStopped(float SuppressDuration)
	{
		bStopVisualIntentActive = true;
		StopStartSuppressTimeRemaining = FMath::Max(StopStartSuppressTimeRemaining, SuppressDuration);
	}

	bool ConsumeStopStartSuppress(float DeltaTime)
	{
		const bool bSuppress = StopStartSuppressTimeRemaining > 0.0f;
		StopStartSuppressTimeRemaining = FMath::Max(0.0f, StopStartSuppressTimeRemaining - DeltaTime);
		return bSuppress;
	}

	void ClearStopVisualIntent() { bStopVisualIntentActive = false; }
	bool IsStopVisualIntentActive() const { return bStopVisualIntentActive; }

	void OnTurnStarted(int32 Sequence, float ServerStartAgeSeconds, uint8 DirectionBucket, float TargetFacingYaw)
	{
		constexpr float PresentationDuration = 2.5f;
		constexpr float MinimumDuration = 0.20f;
		TurnSequence = Sequence;
		TurnDirectionBucket = DirectionBucket;
		TurnTargetFacingYaw = FRotator::NormalizeAxis(TargetFacingYaw);
		TurnTimeRemaining = FMath::Max(MinimumDuration,
			PresentationDuration - FMath::Max(ServerStartAgeSeconds, 0.0f));
		bTurnActive = true;
	}

	void AdvanceTurn(float DeltaTime)
	{
		TurnTimeRemaining = FMath::Max(0.0f, TurnTimeRemaining - DeltaTime);
		if (TurnTimeRemaining <= 0.0f)
		{
			bTurnActive = false;
			TurnDirectionBucket = 0;
			TurnTargetFacingYaw = 0.0f;
		}
	}

	void CancelTurn()
	{
		bTurnActive = false;
		TurnTimeRemaining = 0.0f;
		TurnDirectionBucket = 0;
	}

	bool IsTurnActive() const { return bTurnActive; }
	uint8 GetTurnDirectionBucket() const { return TurnDirectionBucket; }
	float GetTurnTargetFacingYaw() const { return TurnTargetFacingYaw; }
	int32 GetTurnSequence() const { return TurnSequence; }

private:
	float StopStartSuppressTimeRemaining = 0.0f;
	bool bStopVisualIntentActive = false;
	bool bTurnActive = false;
	float TurnTimeRemaining = 0.0f;
	uint8 TurnDirectionBucket = 0;
	float TurnTargetFacingYaw = 0.0f;
	int32 TurnSequence = 0;
};
