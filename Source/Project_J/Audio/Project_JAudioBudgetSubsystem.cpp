#include "Audio/Project_JAudioBudgetSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void UProject_JAudioBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}

void UProject_JAudioBudgetSubsystem::Deinitialize()
{
	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}
	CachedListenerFrame = MAX_uint64;
	CachedListenerWorld.Reset();
	CachedListenerPositions.Reset();
	LastPruneFrame = MAX_uint64;
	ResetAllVoices();
	Super::Deinitialize();
}

void UProject_JAudioBudgetSubsystem::OnWorldCleanup(UWorld* World, bool, bool)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		if (CachedListenerWorld.Get() == World)
		{
			CachedListenerFrame = MAX_uint64;
			CachedListenerWorld.Reset();
			CachedListenerPositions.Reset();
		}
		ActiveCounts.Reset(); // Compatibility for native callers with unscoped manual reservations.
		TArray<TWeakObjectPtr<UAudioComponent>, TInlineAllocator<TotalVoiceHardCap>> Retired;
		for (const auto& Pair : ActivePlayback)
			if (Pair.Value.World.Get() == World) Retired.Add(Pair.Key);
		for (const auto& Component : Retired) RetirePlayback(Component, true);
	}
}

int32 UProject_JAudioBudgetSubsystem::GetGroupVoiceCap(EProject_JAudioGroup Group)
{
	switch (Group)
	{
	case EProject_JAudioGroup::UIAndBossWarning: return 8;
	case EProject_JAudioGroup::LocalPlayer:      return 12;
	case EProject_JAudioGroup::BossSFX:          return 8;
	case EProject_JAudioGroup::PartySFX:         return 8;
	case EProject_JAudioGroup::OtherPlayerSFX:   return 6;
	case EProject_JAudioGroup::AmbienceAndMusic: return 6;
	default:                                     return 4;
	}
}

float UProject_JAudioBudgetSubsystem::GetGroupDuckingMultiplier(EProject_JAudioGroup Group)
{
	switch (Group)
	{
	case EProject_JAudioGroup::PartySFX:       return 0.707f; // ~ -3 dB
	case EProject_JAudioGroup::OtherPlayerSFX: return 0.354f; // ~ -9 dB
	default:                                   return 1.0f;
	}
}

FProject_JAudioAdmissionResult UProject_JAudioBudgetSubsystem::GetAdmission(EProject_JAudioGroup Group, double DistanceToListenerCm) const
{
	FProject_JAudioAdmissionResult Res;
	if (Group == EProject_JAudioGroup::OtherPlayerSFX && DistanceToListenerCm > OtherPlayerCullDistanceCm)
	{
		return Res;
	}
	const int32 GroupCount = GetActiveGroupVoices(Group);
	if (GroupCount >= GetGroupVoiceCap(Group) || GetActiveTotalVoices() >= TotalVoiceHardCap)
	{
		return Res;
	}
	Res.bAdmitted = true;
	Res.VolumeMultiplier = GetGroupDuckingMultiplier(Group);
	return Res;
}

FProject_JAudioAdmissionResult UProject_JAudioBudgetSubsystem::RequestVoice(EProject_JAudioGroup Group, double DistanceToListenerCm)
{
	PrunePlayback();
	const auto Admission = GetAdmission(Group, DistanceToListenerCm);
	if (Admission.bAdmitted) ++ActiveCounts.FindOrAdd(Group);
	return Admission;
}

void UProject_JAudioBudgetSubsystem::ReleaseVoice(EProject_JAudioGroup Group)
{
	if (int32* Count = ActiveCounts.Find(Group))
	{
		*Count = FMath::Max(0, *Count - 1);
	}
}

bool UProject_JAudioBudgetSubsystem::PlayBudgetedSoundAtLocation(
	UObject* WorldContextObject,
	USoundBase* Sound,
	const FVector& Location,
	EProject_JAudioGroup Group,
	float BaseVolumeMultiplier,
	float PitchMultiplier)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_DedicatedServer
		|| World->bIsTearingDown || !IsValid(Sound) || Location.ContainsNaN()
		|| !FMath::IsFinite(BaseVolumeMultiplier) || !FMath::IsFinite(PitchMultiplier))
	{
		return false;
	}

	if (CachedListenerFrame != GFrameCounter || CachedListenerWorld.Get() != World)
	{
		CachedListenerFrame = GFrameCounter;
		CachedListenerWorld = World;
		CachedListenerPositions.Reset();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get(); PC && PC->IsLocalController())
			{
				FVector ListenerPos, Front, Right;
				PC->GetAudioListenerPosition(ListenerPos, Front, Right);
				if (!ListenerPos.ContainsNaN())
				{
					CachedListenerPositions.Add(ListenerPos);
				}
			}
		}
	}

	double MinDistCm = 0.0;
	const bool bHasListener = !CachedListenerPositions.IsEmpty();
	for (int32 i = 0; i < CachedListenerPositions.Num(); ++i)
	{
		const double Dist = FVector::Dist(CachedListenerPositions[i], Location);
		MinDistCm = (i == 0) ? Dist : FMath::Min(MinDistCm, Dist);
	}

	if (!bHasListener && Group == EProject_JAudioGroup::OtherPlayerSFX)
	{
		// Fail closed for non-critical remote player SFX when no local audio listener is active.
		MinDistCm = OtherPlayerCullDistanceCm + 1.0;
	}

	// Fast-reject culled distant remote SFX before scanning ActivePlayback.
	if (Group == EProject_JAudioGroup::OtherPlayerSFX && MinDistCm > OtherPlayerCullDistanceCm)
	{
		return false;
	}

	if (LastPruneFrame != GFrameCounter || GetActiveGroupVoices(Group) >= GetGroupVoiceCap(Group) || GetActiveTotalVoices() >= TotalVoiceHardCap)
	{
		LastPruneFrame = GFrameCounter;
		PrunePlayback();
	}
	const FProject_JAudioAdmissionResult Admission = GetAdmission(Group, MinDistCm);
	if (!Admission.bAdmitted)
	{
		return false;
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
		World, Sound, Location, FRotator::ZeroRotator,
		FMath::Max(0.0f, BaseVolumeMultiplier) * Admission.VolumeMultiplier,
		FMath::Clamp(PitchMultiplier, 0.1f, 4.0f));
	if (!IsValid(Component) || !Component->IsPlaying()) return false;
	ActivePlayback.Add(Component, FTrackedVoice{World, Group});
	++PlaybackCounts.FindOrAdd(Group);
	Component->OnAudioFinishedNative.AddUObject(this, &ThisClass::OnPlaybackFinished);
	return true;
}

void UProject_JAudioBudgetSubsystem::OnPlaybackFinished(UAudioComponent* Component)
{
	RetirePlayback(Component, false);
}

void UProject_JAudioBudgetSubsystem::RetirePlayback(TWeakObjectPtr<UAudioComponent> Component, bool bStop)
{
	FTrackedVoice Voice;
	if (!ActivePlayback.RemoveAndCopyValue(Component, Voice)) return;
	--PlaybackCounts.FindChecked(Voice.Group);
	if (auto* Audio = Component.Get())
	{
		Audio->OnAudioFinishedNative.RemoveAll(this);
		if (bStop) Audio->Stop();
	}
}

void UProject_JAudioBudgetSubsystem::PrunePlayback()
{
	// The engine omits the finish delegate when an async playback cannot start.
	// Reclaim those voices on the next admission using the same bounded tracking set.
	TArray<TWeakObjectPtr<UAudioComponent>, TInlineAllocator<TotalVoiceHardCap>> Finished;
	for (const auto& Pair : ActivePlayback)
	{
		const auto* Component = Pair.Key.Get();
		if (!Component || !Component->IsPlaying()) Finished.Add(Pair.Key);
	}
	for (const auto& Component : Finished) RetirePlayback(Component, false);
}

int32 UProject_JAudioBudgetSubsystem::GetActiveTotalVoices() const
{
	int32 Total = 0;
	for (const auto& Pair : ActiveCounts)
	{
		Total += Pair.Value;
	}
	return Total + ActivePlayback.Num();
}

int32 UProject_JAudioBudgetSubsystem::GetActiveGroupVoices(EProject_JAudioGroup Group) const
{
	return ActiveCounts.FindRef(Group) + PlaybackCounts.FindRef(Group);
}

void UProject_JAudioBudgetSubsystem::ResetAllVoices()
{
	ActiveCounts.Reset();
	TArray<TWeakObjectPtr<UAudioComponent>> Components;
	ActivePlayback.GenerateKeyArray(Components);
	for (const auto& Component : Components) RetirePlayback(Component, true);
	PlaybackCounts.Reset();
}
