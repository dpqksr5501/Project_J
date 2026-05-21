// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JLocomotionAnimationModifier.h"

#include "Animation/AnimSequence.h"
#include "Animation/Project_JLocomotionAnimNotify.h"
#include "AnimationBlueprintLibrary.h"

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
		UAnimNotify* Notify = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
			AnimationSequence,
			NotifyTrackName,
			NotifyTime,
			UProject_JLocomotionAnimNotify::StaticClass());

		if (UProject_JLocomotionAnimNotify* LocomotionNotify = Cast<UProject_JLocomotionAnimNotify>(Notify))
		{
			LocomotionNotify->EventType = Rule.EventType;
		}
	}
}
