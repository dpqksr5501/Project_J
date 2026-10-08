#include "Audio/Project_JFoleyAudioProfile.h"
#include "Sound/SoundBase.h"

const FProject_JFoleySoundSet* UProject_JFoleyAudioProfile::ResolveEvent(FGameplayTag Event, FGameplayTag* OutResolvedEvent) const
{
	if (OutResolvedEvent) { *OutResolvedEvent = FGameplayTag(); }
	TArray<FGameplayTag, TInlineAllocator<MaxFallbackDepth>> Visited;
	for (int32 Depth = 0; Depth < MaxFallbackDepth && Event.IsValid(); ++Depth)
	{
		if (Visited.Contains(Event)) { return nullptr; }
		Visited.Add(Event);
		if (const auto* Definition = Events.Find(Event); Definition && Definition->HasSound())
		{
			if (OutResolvedEvent) { *OutResolvedEvent = Event; }
			return Definition;
		}
		const auto* Fallback = EventFallbacks.Find(Event);
		if (!Fallback) { return nullptr; }
		Event = *Fallback;
	}
	return nullptr;
}

TSoftObjectPtr<USoundBase> FProject_JFoleySoundSet::ResolveSound(EPhysicalSurface Surface) const
{
	if (const auto* Sound = SurfaceSounds.Find(Surface); Sound && !Sound->IsNull()) { return *Sound; }
	return DefaultSound;
}

bool FProject_JFoleySoundSet::HasSound() const
{
	if (!DefaultSound.IsNull()) { return true; }
	for (const auto& Pair : SurfaceSounds) { if (!Pair.Value.IsNull()) { return true; } }
	return false;
}

void UProject_JFoleyAudioProfile::GatherSoundPaths(TArray<FSoftObjectPath>& OutPaths) const
{
	for (const auto& Pair : Events)
	{
		if (!Pair.Value.DefaultSound.IsNull()) { OutPaths.AddUnique(Pair.Value.DefaultSound.ToSoftObjectPath()); }
		for (const auto& Surface : Pair.Value.SurfaceSounds)
		{
			if (!Surface.Value.IsNull()) { OutPaths.AddUnique(Surface.Value.ToSoftObjectPath()); }
		}
	}
}

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"

EDataValidationResult UProject_JFoleyAudioProfile::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	auto Error = [&Context, &bHasError](const FString& Message)
	{
		Project_J::DataValidation::AddError(Context, bHasError, FText::FromString(Message));
	};
	if (!FMath::IsFinite(AudibleDistance) || AudibleDistance < 100.f || AudibleDistance > 5000.f ||
		!FMath::IsFinite(OtherCharacterVolume) || OtherCharacterVolume < 0.f || OtherCharacterVolume > 1.f ||
		!FMath::IsFinite(TraceAboveContact) || TraceAboveContact < 0.f || TraceAboveContact > 100.f ||
		!FMath::IsFinite(TraceBelowContact) || TraceBelowContact < 1.f || TraceBelowContact > 200.f)
	{
		Error(TEXT("Foley distance, trace extents and remote volume must be finite and within their authoring ranges."));
	}
	for (const auto& Pair : Events)
	{
		const auto& Set = Pair.Value;
		if (!Pair.Key.IsValid() || uint8(Set.Group) > uint8(EProject_JFoleyGroup::Other) ||
			uint8(Set.Contact) > uint8(EProject_JFoleyContact::Socket) ||
			!FMath::IsFinite(Set.Importance) || Set.Importance < 0.1f || Set.Importance > 4.f ||
			(Set.Contact == EProject_JFoleyContact::Socket && Set.ContactSocket.IsNone()))
		{
			Error(FString::Printf(TEXT("Invalid Foley event policy: %s"), *Pair.Key.ToString()));
		}
		auto CheckSound = [&Error, &Pair](const TSoftObjectPtr<USoundBase>& Sound)
		{
			// Validation never synchronously loads all clips. Loaded looping clips are still rejected here.
			if (Sound.IsValid() && Sound.Get()->IsLooping())
			{
				Error(FString::Printf(TEXT("Foley event %s requires a one-shot sound."), *Pair.Key.ToString()));
			}
		};
		CheckSound(Set.DefaultSound);
		for (const auto& Surface : Set.SurfaceSounds) { CheckSound(Surface.Value); }
	}
	for (const auto& Pair : EventFallbacks)
	{
		TArray<FGameplayTag, TInlineAllocator<MaxFallbackDepth>> Visited;
		FGameplayTag Cursor = Pair.Key;
		for (int32 Depth = 0; ; ++Depth)
		{
			if (Depth >= MaxFallbackDepth || Visited.Contains(Cursor))
			{
				Error(FString::Printf(TEXT("Cyclic or overlong Foley fallback graph starting at %s."), *Pair.Key.ToString()));
				break;
			}
			Visited.Add(Cursor);
			const auto* Next = EventFallbacks.Find(Cursor);
			if (!Next) { break; }
			Cursor = *Next;
		}
		if (!Pair.Key.IsValid() || !Pair.Value.IsValid() || !ResolveEvent(Pair.Key))
		{
			Error(FString::Printf(TEXT("Foley fallback %s must resolve to a populated event within %d definitions (no cycles)."),
				*Pair.Key.ToString(), MaxFallbackDepth));
		}
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
