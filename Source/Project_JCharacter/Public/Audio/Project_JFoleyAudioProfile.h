#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "Audio/Project_JFoleyTypes.h"
#include "Project_JFoleyAudioProfile.generated.h"

class USoundBase;
class USoundAttenuation;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFoleySoundSet
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	EProject_JFoleyGroup Group = EProject_JFoleyGroup::Footstep;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	EProject_JFoleyContact Contact = EProject_JFoleyContact::Foot;
	/** Used on the visual/source mesh for Socket mode; absent/stale poses use capsule contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact", meta = (EditCondition = "Contact == EProject_JFoleyContact::Socket"))
	FName ContactSocket;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TSoftObjectPtr<USoundBase> DefaultSound;
	/** Unassigned surfaces fall back to DefaultSound. No level-specific names are required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TMap<TEnumAsByte<EPhysicalSurface>, TSoftObjectPtr<USoundBase>> SurfaceSounds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bTraceSurface = true;
	/** Perceptual selection weight within the remote cohort, not gameplay priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float Importance = 1.0f;

	TSoftObjectPtr<USoundBase> ResolveSound(EPhysicalSurface Surface) const;
	bool HasSound() const;
};

/** Client presentation data. Soft sounds are streamed once per shared profile, never on the server. */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JFoleyAudioProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	/** Exact definitions win; fallback rules are opt-in and never inferred from tag parents. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TMap<FGameplayTag, FProject_JFoleySoundSet> Events;
	/** Routes an absent/empty event to another complete definition. Cycles remain silent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TMap<FGameplayTag, FGameplayTag> EventFallbacks;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName SourceLeftFoot = TEXT("foot_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName SourceRightFoot = TEXT("foot_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName VisualLeftFoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName VisualRightFoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName SourceLeftHand = TEXT("hand_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName SourceRightHand = TEXT("hand_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName VisualLeftHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	FName VisualRightHand;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact")
	TEnumAsByte<ECollisionChannel> SurfaceTraceChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float TraceAboveContact = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Contact", meta = (ClampMin = "1.0", ClampMax = "200.0"))
	float TraceBelowContact = 80.0f;
	/** Receiver-side admission radius in cm; also used by the generated attenuation fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Mix", meta = (ClampMin = "100.0", ClampMax = "5000.0"))
	float AudibleDistance = 1800.0f;
	/** Optional artist-authored attenuation. Admission radius remains a separate CPU bound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Mix")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley|Mix", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OtherCharacterVolume = 0.6f;

	void GatherSoundPaths(TArray<FSoftObjectPath>& OutPaths) const;
	static constexpr int32 MaxFallbackDepth = 16;
	const FProject_JFoleySoundSet* ResolveEvent(FGameplayTag Event, FGameplayTag* OutResolvedEvent = nullptr) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
