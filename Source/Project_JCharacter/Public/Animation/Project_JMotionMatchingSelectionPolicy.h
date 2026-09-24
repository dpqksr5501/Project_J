#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** 게임 스레드의 의미 선택 리비전과 동일 데이터베이스 방향 전환 재선택 간격을 관리한다. */
class FProject_JMotionMatchingSelectionPolicy
{
public:
	struct FKey
	{
		EProject_JLocomotionGaitIntent Gait = EProject_JLocomotionGaitIntent::Run;
		EProject_JLocomotionRotationMode Rotation = EProject_JLocomotionRotationMode::OrientToMovement;
		EProject_JLocomotionPhaseFamily Phase = EProject_JLocomotionPhaseFamily::Idle;
		EProject_JGroundMotionMode GroundMode = EProject_JGroundMotionMode::Idle;
		bool bUseSettledCycle = false;
	};

	void Reset()
	{
		bHasPublished = false;
		LastPublished = {};
		LastReselectTimeSeconds = -DBL_MAX;
		AdvanceRevision();
	}

	bool Publish(const FKey& Key)
	{
		const bool bChanged = !bHasPublished ||
			LastPublished.Gait != Key.Gait ||
			LastPublished.Rotation != Key.Rotation ||
			LastPublished.Phase != Key.Phase ||
			LastPublished.GroundMode != Key.GroundMode ||
			LastPublished.bUseSettledCycle != Key.bUseSettledCycle;
		if (bChanged)
		{
			LastPublished = Key;
			bHasPublished = true;
			AdvanceRevision();
		}
		return bChanged;
	}

	bool RequestRedirectReselect(bool bRequested, double NowSeconds, float CooldownSeconds)
	{
		if (!bRequested || NowSeconds - LastReselectTimeSeconds < CooldownSeconds)
		{
			return false;
		}
		LastReselectTimeSeconds = NowSeconds;
		return true;
	}

	int32 GetRevision() const { return Revision; }
	bool HasPublished() const { return bHasPublished; }
	EProject_JLocomotionRotationMode GetLastPublishedRotationMode() const { return LastPublished.Rotation; }

private:
	void AdvanceRevision()
	{
		++Revision;
		if (Revision == 0)
		{
			Revision = 1;
		}
	}

	FKey LastPublished;
	double LastReselectTimeSeconds = -DBL_MAX;
	int32 Revision = 1;
	bool bHasPublished = false;
};
