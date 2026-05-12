// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JBaseCharacter.h"
#include "Logging/LogMacros.h"
#include "Project_JPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
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

	// 착지 타이머 핸들
	FTimerHandle LandingTimerHandle;

	// 점프 타이머 핸들
	FTimerHandle JumpTimerHandle;

	FTimerHandle FallOffStartTimerHandle;

	bool bWasInAir = false;

	bool bSuppressFallOffStart = false;

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

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** 츄저 테이블(Chooser Table)에서 읽어갈 착지 상태 플래그 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bIsLanding = false;

	/** True while the character should be treated as airborne by the Anim Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsInAir = false;

	/** 츄저 테이블(Chooser Table)에서 읽어갈 점프 시작 상태 플래그 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsJumping = false;

	/** True briefly when entering air from walking/running off ground instead of a jump. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsFallOffStart = false;

	/** 츄저 테이블용: 실시간 Z축 속도 (상승/하강 판별) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float VerticalSpeed = 0.0f;

	/** 츄저 테이블용: 실시간 수평 이동 속도 (Idle/Run 판별) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float GroundSpeed = 0.0f;

	/** 착지 시점의 하강 속도 절대값 (하드/소프트 착지 분기용) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	float LastFallSpeed = 0.0f;
};

