#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Audio/Project_JFoleyTypes.h"
#include "Project_JFoleyComponent.generated.h"

class UProject_JFoleyAudioProfile;
class USkeletalMeshComponent;
class ACharacter;
struct FProject_JFoleySoundSet;

/** Avatar-local cosmetic events; no component Tick, replication or persistent PlayerState state. */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JFoleyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JFoleyComponent();
	/** NPC/default fallback; player CharacterAnimProfile is used when this is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TSoftObjectPtr<UProject_JFoleyAudioProfile> ProfileOverride;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	bool bEnabled = true;
	/** The production player already supplies accepted Jump/Land boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bUseMovementEventsForJumpAndLand = true;
	UFUNCTION(BlueprintCallable, Category = "Foley")
	bool PlayFoleyEvent(const FProject_JFoleyEvent& Event);
	/** Ownership/profile activation hook; requests local audio without submitting a contact. */
	bool PrepareLocalAudio();
	void HandleAnimNotify(USkeletalMeshComponent* Mesh, const FProject_JFoleyEvent& Event);
	void PlayJump(float HorizontalSpeed, float AgeSeconds = 0.0f);
	void PlayLanding(float ImpactSpeed, float AgeSeconds = 0.0f);

	TSoftObjectPtr<UProject_JFoleyAudioProfile> GetEffectiveProfile() const;
	bool CanPresent(EProject_JFoleyGroup Group) const;
	FVector ResolveContactLocation(const UProject_JFoleyAudioProfile& Profile, EProject_JFoleySide Side,
		const FProject_JFoleySoundSet* Definition = nullptr) const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class UProject_JFoleySubsystem;
	friend class FProjectJFoleyLifetimeTest;
	/** Bounded family/side storage, never stored on the shared AnimNotify UObject. */
	double LastAdmission[15];
};
