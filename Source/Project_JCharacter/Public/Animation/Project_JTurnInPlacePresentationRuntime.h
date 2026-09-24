#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"

/** 게임 스레드에서 저작된 TIP 선택과 시각적 yaw 전송 정책을 관리한다. */
class FProject_JTurnInPlacePresentationRuntime
{
public:
	enum class ESelectionUpdate : uint8
	{
		NewSelection,
		Continuing,
		AwaitingFreshSelection
	};

	void Reset() { *this = FProject_JTurnInPlacePresentationRuntime(); }

	ESelectionUpdate UpdateSelection(const UAnimSequence* Sequence, int32 SelectionRevision,
		float ActorYaw, float HoldElapsed)
	{
		if (SelectedSequence.Get() != Sequence || SelectedRevision != SelectionRevision)
		{
			SelectedSequence = const_cast<UAnimSequence*>(Sequence);
			SelectedRevision = SelectionRevision;
			SelectionStartActorYaw = ActorYaw;
			LastObservedHoldElapsed = HoldElapsed;
			bAwaitingFreshSelection = false;
			return ESelectionUpdate::NewSelection;
		}
		// The hold clock can restart one frame before the thread-safe chooser
		// publishes its new asset/revision. Applying the old cumulative root yaw
		// against the reset clock would snap the capsule back toward its old anchor.
		if (bAwaitingFreshSelection || HoldElapsed + 0.02f < LastObservedHoldElapsed)
		{
			bAwaitingFreshSelection = true;
			return ESelectionUpdate::AwaitingFreshSelection;
		}
		LastObservedHoldElapsed = FMath::Max(LastObservedHoldElapsed, HoldElapsed);
		return ESelectionUpdate::Continuing;
	}

	float GetSelectionStartActorYaw() const { return SelectionStartActorYaw; }

	static float ClampAuthoredYaw(float RootYawDelta, float FacingDelta)
	{
		if (RootYawDelta > 0.0f)
		{
			return FMath::Min(RootYawDelta, FMath::Max(FacingDelta, 0.0f));
		}
		if (RootYawDelta < 0.0f)
		{
			return FMath::Max(RootYawDelta, FMath::Min(FacingDelta, 0.0f));
		}
		return 0.0f;
	}

	bool ShouldSendActiveYaw(float CurrentActorYaw, double NowSeconds)
	{
		const bool bYawChanged = FMath::Abs(FRotator::NormalizeAxis(CurrentActorYaw - LastSentActorYaw)) >= 2.0f;
		const bool bTimeElapsed = NowSeconds - LastSendTimeSeconds >= 0.05;
		if (bLastSentActive && !(bYawChanged && bTimeElapsed))
		{
			return false;
		}
		bLastSentActive = true;
		LastSentActorYaw = CurrentActorYaw;
		LastSendTimeSeconds = NowSeconds;
		return true;
	}

	bool FinishFrame(bool bCurrentlyInTurn, bool bApplyingLocalYaw, float ActorYaw,
		bool bMaySendInactive)
	{
		const bool bSendInactive = bMaySendInactive && bLastSentActive && !bApplyingLocalYaw;
		if (bSendInactive)
		{
			bLastSentActive = false;
			LastSentActorYaw = ActorYaw;
		}
		if (!bCurrentlyInTurn)
		{
			SelectedSequence.Reset();
			SelectedRevision = INDEX_NONE;
			LastObservedHoldElapsed = -1.0f;
			bAwaitingFreshSelection = false;
		}
		return bSendInactive;
	}

private:
	TWeakObjectPtr<UAnimSequence> SelectedSequence;
	int32 SelectedRevision = INDEX_NONE;
	float SelectionStartActorYaw = 0.0f;
	float LastObservedHoldElapsed = -1.0f;
	bool bAwaitingFreshSelection = false;
	bool bLastSentActive = false;
	float LastSentActorYaw = 0.0f;
	double LastSendTimeSeconds = 0.0;
};
