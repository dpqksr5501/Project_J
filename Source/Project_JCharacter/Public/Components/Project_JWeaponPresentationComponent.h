#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JWeaponPresentationComponent.generated.h"

class UProject_JWeaponPresentationProfile;

/**
 * Owns the runtime weapon actor shared by every humanoid job.
 *
 * Equipped-item data selects the actor and socket through WeaponPresentationProfile. Individual
 * job characters therefore do not need a job-named weapon component merely to
 * display their weapon.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JWeaponPresentationComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Shows the currently selected weapon in its authored combat socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void EnterCombatPresentation();

	/** Removes the runtime weapon actor. Draw/sheath notifies can replace this policy later. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void ExitCombatPresentation();

	/** Keeps the drawn weapon alive while a sheathe montage is playing. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void BeginSheathePresentation();

	/** Attaches the visible weapon to the authored back/sheath socket. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void AttachWeaponToSheathedSocket();

	/** Rebuilds the visible weapon when the effective weapon profile changes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void RefreshPresentation();

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	AActor* GetSpawnedWeapon() const { return SpawnedWeapon; }

private:
	const UProject_JWeaponPresentationProfile* GetCurrentPresentationProfile() const;
	bool ShouldShowWeapon() const;
	void LogWeaponPresentationDebug(const TCHAR* Context) const;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Combat|Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AActor> SpawnedWeapon = nullptr;

	float WeaponPresentationDebugElapsedSeconds = 0.0f;
	bool bCombatPresentationActive = false;
};
