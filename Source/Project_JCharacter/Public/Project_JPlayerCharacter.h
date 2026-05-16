// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JBaseCharacter.h"
#include "Logging/LogMacros.h"
#include "Project_JPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UAnimMontage;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A player-controllable third person character
 *  Implements a controllable orbiting camera and motion matching landing logic.
 */
UCLASS(abstract)
class PROJECT_JCHARACTER_API AProject_JPlayerCharacter : public AProject_JBaseCharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Constructor */
	AProject_JPlayerCharacter();	

protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// 매 프레임 속도 연산을 위해 Tick 추가
	virtual void Tick(float DeltaTime) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	void StopMoveInput();

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	// 기본 착지 이벤트 오버라이드(모션매칭)
	virtual void Landed(const FHitResult& Hit) override;

	// 착지 타이머 종료 시 호출
	void OnLandingTimerFinished();

	// 점프 타이머 종료 시 호출
	void OnJumpTimerFinished();

	void OnFallOffStartFinished();

	bool IsInAirForAnimation() const;

	void StartFallOffStart();

	void StopFallOffStart();

	void UpdateMovementRequestState(float DeltaTime);

	void ClearMovementRequests();

	void PlayCombatIntroMontage();

	void CancelCombatIntroMontage();

	UFUNCTION()
	void OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void ApplyCombatRotationMode(bool bEnableCombatRotation);

	void UpdateMaxWalkSpeed();

	// 착지 타이머 핸들
	FTimerHandle LandingTimerHandle;

	// 점프 타이머 핸들
	FTimerHandle JumpTimerHandle;

	FTimerHandle FallOffStartTimerHandle;

	bool bWasInAir = false;

	bool bSuppressFallOffStart = false;

	FVector2D CachedMoveInput = FVector2D::ZeroVector;

	// C++?먯꽌 '吏꾩쭨 李⑹?'濡??먯젙?섏뿀????釉붾（?꾨┛??ABP)濡??좏샇瑜?蹂대궡湲??꾪븳 ?대깽??	
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement|Animation")
	void K2_OnRealLanded();

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	UFUNCTION(BlueprintCallable, Category = "Movement|Animation")
	void FinishFallOffStart();

	UFUNCTION(BlueprintCallable, Category = "Movement|Animation")
	void FinishJumpStart();

	/** Toggles the combat mode on or off */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ToggleCombatMode();

	/** Sets the combat mode to the specified state */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void SetCombatMode(bool bInCombatMode);

	/** Starts combat mode after the combat intro montage finishes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void BeginCombatModeWithIntro();

	/** Stops the combat intro montage when a higher-priority reaction, such as hit react, needs to take over. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void InterruptCombatIntroForHit();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSprint();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** 츄저 테이블(Chooser Table)에서 읽어갈 착지 상태 플래그 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bIsLanding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bLandingRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bCanEnterLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bCanEnterGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 0.45f;

	/** True while the character should be treated as airborne by the Anim Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsPhysicallyInAir = false;

	/** 츄저 테이블(Chooser Table)에서 읽어갈 점프 시작 상태 플래그 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsJumping = false;

	/** Failsafe timeout for clearing JumpStart if the Anim Blueprint does not call FinishJumpStart. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float JumpStartMaxDuration = 1.0f;

	/** True briefly when entering air from walking/running off ground instead of a jump. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsFallOffStart = false;

	/** How long FallOffStart stays true unless landing or an animation notify finishes it first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float FallOffStartDuration = 0.6f;

	/** 츄저 테이블용: 실시간 Z축 속도 (상승/하강 판별) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float VerticalSpeed = 0.0f;

	/** 츄저 테이블용: 실시간 수평 이동 속도 (Idle/Run 판별) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WalkSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NonCombatMovementYawInterpSpeed = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintMovementYawInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WalkRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bSmoothNonCombatMovementDirection = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoveInputDeadZone = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SideMoveInputThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float StartRequestDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartToLoopDelay = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float StopRequestDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RunToSprintSpeedThreshold = 500.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bHasSideMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStartRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float StartRequestTimer = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float StopRequestTimer = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStartToLoopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float StopStartSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bJustStartedMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bWantsToStop = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float SmoothedMovementYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bHasSmoothedMovementYaw = false;

	/** 착지 시점의 하강 속도 절대값 (하드/소프트 착지 분기용) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	float LastFallSpeed = 0.0f;

	// --- Combat States ---

	/** True if the character is currently in combat mode (weapon drawn) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsCombatMode = false;

	/** True if the character is currently attacking */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsAttacking = false;

	/** True if the character is currently dodging */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsDodging = false;

	/** True if the character is currently reacting to a hit */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsHitReacting = false;

	/** 로컬 이동 각도 (-180 ~ 180) : 전투 중 전진/후진/좌우 스트레이프 판별용 */
	/** Upper-body montage played when entering combat mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	UAnimMontage* CombatIntroMontage = nullptr;

	/** Playback rate for CombatIntroMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CombatIntroMontagePlayRate = 1.0f;

	/** True while the combat intro montage is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bIsPlayingCombatIntro = false;

	/** True while combat mode is waiting for the intro montage to finish. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bPendingCombatModeFromIntro = false;

	/** If true, hit reactions can stop the combat intro montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bInterruptCombatIntroOnHit = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float MovementDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatInputForward = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatInputRight = 0.0f;

	/** Camera-relative forward movement speed for combat blend spaces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatForwardSpeed = 0.0f;

	/** Camera-relative right movement speed for combat blend spaces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatRightSpeed = 0.0f;
};
