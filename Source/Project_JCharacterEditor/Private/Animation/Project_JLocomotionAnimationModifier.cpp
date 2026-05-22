// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JLocomotionAnimationModifier.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/Project_JLocomotionAnimNotify.h"
#include "AnimationBlueprintLibrary.h"

namespace
{
bool HasMatchingLocomotionNotify(
	UAnimSequence* AnimationSequence,
	FName NotifyTrackName,
	EProject_JLocomotionAnimEvent EventType,
	float NotifyTime,
	float TimeTolerance)
{
	TArray<FAnimNotifyEvent> ExistingEvents;
	UAnimationBlueprintLibrary::GetAnimationNotifyEventsForTrack(AnimationSequence, NotifyTrackName, ExistingEvents);

	for (const FAnimNotifyEvent& ExistingEvent : ExistingEvents)
	{
		const UProject_JLocomotionAnimNotify* ExistingNotify = Cast<UProject_JLocomotionAnimNotify>(ExistingEvent.Notify);
		if (!ExistingNotify || ExistingNotify->EventType != EventType)
		{
			continue;
		}

		if (FMath::Abs(ExistingEvent.GetTime() - NotifyTime) <= TimeTolerance)
		{
			return true;
		}
	}

	return false;
}

void AddLocomotionNotifyIfMissing(
	UAnimSequence* AnimationSequence,
	FName NotifyTrackName,
	EProject_JLocomotionAnimEvent EventType,
	float NotifyTime,
	float TimeTolerance)
{
	if (HasMatchingLocomotionNotify(AnimationSequence, NotifyTrackName, EventType, NotifyTime, TimeTolerance))
	{
		return;
	}

	UAnimNotify* Notify = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
		AnimationSequence,
		NotifyTrackName,
		NotifyTime,
		UProject_JLocomotionAnimNotify::StaticClass());

	if (UProject_JLocomotionAnimNotify* LocomotionNotify = Cast<UProject_JLocomotionAnimNotify>(Notify))
	{
		LocomotionNotify->EventType = EventType;
	}
}
}

void UProject_JLocomotionAnimationModifier::OnApply_Implementation(UAnimSequence* AnimationSequence)
{
	if (!AnimationSequence)
	{
		return;
	}

	if (bClearExistingProjectJTrack)
	{
		UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByTrack(AnimationSequence, NotifyTrackName);
	}

	TArray<FName> TrackNames;
	UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(AnimationSequence, TrackNames);
	if (!TrackNames.Contains(NotifyTrackName))
	{
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(AnimationSequence, NotifyTrackName);
	}

	const float SequenceLength = AnimationSequence->GetPlayLength();
	for (const FProject_JLocomotionNotifyRule& Rule : NotifyRules)
	{
		const float NotifyTime = FMath::Clamp(Rule.Time, 0.0f, SequenceLength);
		AddLocomotionNotifyIfMissing(
			AnimationSequence,
			NotifyTrackName,
			Rule.EventType,
			NotifyTime,
			DuplicateTimeTolerance);
	}

	if (bAutoAddNotify)
	{
		const float AutoNotifyTime = FMath::Clamp(SequenceLength * AutoNotifyNormalizedTime, 0.0f, SequenceLength);
		AddLocomotionNotifyIfMissing(
			AnimationSequence,
			NotifyTrackName,
			AutoNotifyEventType,
			AutoNotifyTime,
			DuplicateTimeTolerance);
	}
}
