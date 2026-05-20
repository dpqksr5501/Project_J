// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JLocomotionAnimStateComponent.generated.h"

class AProject_JPlayerCharacter;
class UAbilitySystemComponent;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
struct FHitResult;

/**
 * Owns the player locomotion state consumed by animation graphs and Chooser Tables.
 *
 * This component owns the state-machine logic so MMORPG-scale character code can
 * keep animation-facing locomotion separate from player input and combat flow.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Character), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JLocomotionAnimStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JLocomotionAnimStateComponent();

	virtual void BeginPlay() override;

	void UpdateState(float DeltaTime);
	void HandleJumpStarted();
	void HandleLanded(const FHitResult& Hit);
	void FinishLanding();
	void FinishJumpStart();
	void FinishFallOffStart();
	void MarkGroundStartFinished();
	void SetMoveInput(const FVector2D& InMoveInput);
	void ClearMoveInput();
	bool ConsumeRealLandingEventRequested();

protected:
	void CacheOwnerReferences();
	AProject_JPlayerCharacter* GetPlayerOwner() const;
	UCharacterMovementComponent* GetCachedMovementComponent() const;
	UAbilitySystemComponent* GetCachedAbilitySystemComponent() const;
	bool IsInAirForAnimation() const;
	bool ShouldUseLocalInputState() const;
	bool IsDedicatedServerContext() const;
	bool WasRecentlyRendered() const;
	void StartFallOffStart();
	void StopFallOffStart();
	void OnLandingTimerFinished();
	void OnJumpTimerFinished();
	void OnFallOffStartFinished();
	void UpdateMovementRequestState(float DeltaTime);
	void UpdateRemoteMovementRequestState(float DeltaTime);
	void UpdateCombatMovementState(const FVector& HorizontalVelocity);
	void ClearMovementRequests();

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeavyLandSpeedThreshold = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RealLandingEventSpeedThreshold = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float JumpStartMaxDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float FallOffStartDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoveInputDeadZone = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartToLoopDelay = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinStartDatabaseTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RunToSprintSpeedThreshold = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnMinSpeed = 500.0f;

	/** Remote proxies do not have authoritative local input, so derive request state from replicated movement instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bUseInputDerivedRequestsForRemotePlayers = false;

	/** Minimum replicated movement speed treated as movement input for remote proxies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteMoveSpeedThreshold = 3.0f;

	/** Window used by WasRecentlyRendered for future update-rate throttling decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Visibility", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RecentlyRenderedTolerance = 0.25f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bUsingLocalInputState = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bRecentlyRendered = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bDedicatedServerContext = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bIsLanding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bLandingRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bCanEnterLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bCanEnterGround = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsPhysicallyInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsJumping = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Jumping")
	bool bIsFallOffStart = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn")
	bool bSharpTurnRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStartRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bUseStartDatabase = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bGroundStartFinished = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bPendingGroundStartFinish = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	float LastFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	float LandStartGroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	float LandStartFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bLandWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bLandWasMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bUseHeavyLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float MovementDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatInputForward = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatInputRight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatForwardSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float CombatRightSpeed = 0.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<AProject_JPlayerCharacter> CachedPlayerOwner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CachedMovementComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> CachedAbilitySystemComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMeshComponent = nullptr;

	FTimerHandle LandingTimerHandle;
	FTimerHandle JumpTimerHandle;
	FTimerHandle FallOffStartTimerHandle;

	FVector2D CachedMoveInput = FVector2D::ZeroVector;
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;
	bool bWasInAir = false;
	bool bSuppressFallOffStart = false;
	bool bRealLandingEventRequested = false;
};
