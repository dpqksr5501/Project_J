#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JAudioBudgetSubsystem.generated.h"

class USoundBase;
class UAudioComponent;

/** 48-voice hard-capped audio concurrency groups for 200-player boss fights (BP06). */
UENUM(BlueprintType)
enum class EProject_JAudioGroup : uint8
{
	UIAndBossWarning, // Cap 8
	LocalPlayer,      // Cap 12
	BossSFX,          // Cap 8
	PartySFX,         // Cap 8 (-3dB ducking)
	OtherPlayerSFX,   // Cap 6 (25m cull, -9dB ducking)
	AmbienceAndMusic  // Cap 6
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JAudioAdmissionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	bool bAdmitted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	float VolumeMultiplier = 1.0f;
};

/**
 * Enforces the 48-voice concurrency & distance-culling audio budget across all sound emitters (BP06).
 */
UCLASS()
class PROJECT_J_API UProject_JAudioBudgetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 TotalVoiceHardCap = 48;
	static constexpr double OtherPlayerCullDistanceCm = 2500.0;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static int32 GetGroupVoiceCap(EProject_JAudioGroup Group);
	static float GetGroupDuckingMultiplier(EProject_JAudioGroup Group);

	/** Evaluates admission against per-group cap, global 48-voice cap, and distance cull. */
	FProject_JAudioAdmissionResult RequestVoice(EProject_JAudioGroup Group, double DistanceToListenerCm);
	void ReleaseVoice(EProject_JAudioGroup Group);

	/**
	 * One-call runtime/Blueprint playback bridge enforcing the 48-voice cap, 25m other-player cull,
	 * group ducking, and automatic voice release on sound completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Budget", meta = (WorldContext = "WorldContextObject"))
	bool PlayBudgetedSoundAtLocation(
		UObject* WorldContextObject,
		USoundBase* Sound,
		const FVector& Location,
		EProject_JAudioGroup Group,
		float BaseVolumeMultiplier = 1.0f,
		float PitchMultiplier = 1.0f);

	int32 GetActiveTotalVoices() const;
	int32 GetActiveGroupVoices(EProject_JAudioGroup Group) const;
	void ResetAllVoices();

private:
	struct FTrackedVoice
	{
		TWeakObjectPtr<UWorld> World;
		EProject_JAudioGroup Group;
	};
	FProject_JAudioAdmissionResult GetAdmission(EProject_JAudioGroup Group, double DistanceToListenerCm) const;
	void OnPlaybackFinished(UAudioComponent* Component);
	void RetirePlayback(TWeakObjectPtr<UAudioComponent> Component, bool bStop);
	void PrunePlayback();
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	FDelegateHandle WorldCleanupHandle;
	TMap<EProject_JAudioGroup, int32> ActiveCounts;
	TMap<EProject_JAudioGroup, int32> PlaybackCounts;
	TMap<TWeakObjectPtr<UAudioComponent>, FTrackedVoice> ActivePlayback;
	uint64 CachedListenerFrame = MAX_uint64;
	TWeakObjectPtr<UWorld> CachedListenerWorld;
	TArray<FVector, TInlineAllocator<2>> CachedListenerPositions;
	uint64 LastPruneFrame = MAX_uint64;
};
