// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatAnimationLayerComponent.generated.h"

struct FStreamableHandle;

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

	/** Call after equipment, combat, or mount state changes. */
	void RefreshLayer();

	/** Resolve the current weapon Anim Layer before a combat transition needs it. */
	void PreloadLayerForCurrentWeapon();

	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	EProject_JCombatAnimationLayerState GetPresentationState() const { return PresentationState; }

private:
	EProject_JCombatAnimationLayerState CalculatePresentationState(const class AProject_JPlayerCharacter& Player) const;
	FSoftObjectPath ResolveLayerPath(const class UProject_JWeaponAnimProfile* WeaponProfile) const;
	void HandleLayerPreloadCompleted(FSoftObjectPath RequestedPath);
	void UnlinkLayer();
	void ResetPreload();

	/** Hard reference held only while the weapon layer is actively linked. */
	TSubclassOf<class UAnimInstance> LinkedAnimationLayerClass;

	/** Cached preloaded class for the equipped weapon profile. */
	UPROPERTY(Transient)
	TSubclassOf<class UAnimInstance> PreloadedAnimationLayerClass;

	FSoftObjectPath PreloadedAnimationLayerPath;
	TSharedPtr<FStreamableHandle> LayerPreloadHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation", meta = (AllowPrivateAccess = "true"))
	EProject_JCombatAnimationLayerState PresentationState = EProject_JCombatAnimationLayerState::Inactive;
};
