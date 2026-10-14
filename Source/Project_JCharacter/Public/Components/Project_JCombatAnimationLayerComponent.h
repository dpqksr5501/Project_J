// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/Project_JAnimationLayerLease.h"
#include "Project_JCombatAnimationLayerComponent.generated.h"


/** Presentation-only state with explicit priority for linked weapon Anim Layers. */
UENUM(BlueprintType)
enum class EProject_JCombatAnimationLayerState : uint8
{
	Inactive,
	PreparingCombat,
	CombatActive,
	SuppressedByMount
};

/**
 * Links the equipped weapon family's Anim Layer into whichever production
 * humanoid master AnimBP is assigned by the owning job Blueprint.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCombatAnimationLayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCombatAnimationLayerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Call after equipment, combat, or mount state changes. */
	void RefreshLayer();

	/** Resolve the current weapon Anim Layer before a combat transition needs it. */
	void PreloadLayerForCurrentWeapon();

	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	EProject_JCombatAnimationLayerState GetPresentationState() const { return PresentationState; }

	/** Explicit retry after a failed path; normal Refresh never retries a failure. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Layers")
	void RetryLayerLoad();

private:
	EProject_JCombatAnimationLayerState CalculatePresentationState(const class AProject_JPlayerCharacter& Player) const;
	FSoftObjectPath ResolveLayerPath(const class UProject_JWeaponAnimProfile* WeaponProfile) const;
	void HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath, uint64 RequestGeneration);
	void UnlinkLayer();
	void ResetPreload();

	UPROPERTY(Transient, DuplicateTransient)
	FProject_JAnimationLayerLease Layer;
	bool bEndingPlay = false;
	friend class FProjectJLayerLoadLifecycleTest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation", meta = (AllowPrivateAccess = "true"))
	EProject_JCombatAnimationLayerState PresentationState = EProject_JCombatAnimationLayerState::Inactive;
};
