// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Project_JCharacterAnimInstanceBase.generated.h"

class ACharacter;
class APawn;
class AProject_JPlayerCharacter;
class UProject_JLocomotionAnimStateComponent;

/**
 * Shared owner/reference cache for native character animation instances.
 *
 * Motion matching, Chooser evaluation, and player-specific state stay in the
 * concrete anim instance; this base only owns reusable owner lookup.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstanceBase : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;

protected:
	void CacheOwnerReferences();
	bool NeedsOwnerReferenceRefresh() const;
	bool IsDedicatedServerAnimationContext() const;
	bool IsLocallyControlledCharacter() const;
	bool WasOwnerRecentlyRendered(float RecentlyRenderedTolerance) const;
	float CalculateViewerDistanceSquared() const;

	UPROPERTY(Transient)
	TObjectPtr<APawn> OwningPawn = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwningCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> OwningPlayerCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JLocomotionAnimStateComponent> LocomotionAnimStateComponent = nullptr;
};
