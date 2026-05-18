// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Project_JCharacterAnimInstance.generated.h"

class ACharacter;
class APawn;
class AProject_JPlayerCharacter;

/**
 * Native animation data bridge for Project J characters.
 *
 * Keep Anim Blueprints focused on state machines, choosers, and graph logic.
 * Character state is copied here once per animation update instead of being
 * rebuilt in every ABP Event Graph.
 */
UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Animation|Movement")
	void MarkGroundStartFinished();

protected:
	void CacheOwningCharacter();
	void ResetAnimationState();
	void UpdateFromGenericCharacter(float DeltaSeconds);
	void UpdateFromPlayerCharacter(float DeltaSeconds, const AProject_JPlayerCharacter& PlayerCharacter);
	void UpdateAimOffset();
	float CalculateAimOffsetAlpha() const;

public:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|References")
	TObjectPtr<APawn> OwningPawn = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|References")
	TObjectPtr<ACharacter> OwningCharacter = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|References")
	TObjectPtr<AProject_JPlayerCharacter> OwningPlayerCharacter = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	float DeltaTime = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	float VerticalSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	float LastFallSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	float LandStartGroundSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	float LandStartFallSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bLandWasSprinting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bLandWasMoving = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bUseHeavyLand = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsPhysicallyInAir = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsJumping = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Movement")
	bool bIsFallOffStart = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bIsLanding = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bLandingRequested = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bCanEnterLand = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Landing")
	bool bCanEnterGround = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	float MoveInputSize = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input|Turn")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bHasMoveInput = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input|Turn")
	bool bSharpTurnRequested = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bStartRequested = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bUseStartDatabase = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bGroundStartFinished = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bPendingGroundStartFinish = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bStartWasSprinting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	bool bStopRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GenericMoveInputSpeedThreshold = 3.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input")
	float RunToSprintSpeedThreshold = 500.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input|Turn")
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Input|Turn")
	float SharpTurnMinSpeed = 500.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsCombatMode = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsAttacking = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsDodging = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsHitReacting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bIsPlayingCombatIntro = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	bool bPendingCombatModeFromIntro = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	float MovementDirection = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputForward = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	float CombatInputRight = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	float CombatForwardSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Combat")
	float CombatRightSpeed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimYaw = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimPitch = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|AimOffset")
	float AimOffsetAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimYaw = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimPitch = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StandingAimAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MovingAimAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SprintAimAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CombatAimAlpha = 1.0f;
};
