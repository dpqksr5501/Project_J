// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JLocomotionAnimStateComponent.generated.h"

class AProject_JPlayerCharacter;
class UAbilitySystemComponent;
class UCharacterMovementComponent;
class UCapsuleComponent;
class USkeletalMeshComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class EProject_JLocomotionAnimEvent : uint8
{
	GroundStartFinished,
	StopFinished,
	JumpStartFinished,
	FallOffStartFinished,
	LandingFinished,
	HitReactFinished,
	AttackFinished
};

UENUM(BlueprintType)
enum class EProject_JGroundMotionMode : uint8
{
	Idle,
	Start,
	Locomotion,
	Stop
};

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
	void HandleReplicatedJumpStarted();
	void HandleLanded(const FHitResult& Hit);
	void FinishLanding(bool bForceFinish = false);
	void FinishStop();
	void FinishJumpStart();
	void FinishFallOffStart();
	void MarkGroundStartFinished();
	void SetMoveInput(const FVector2D& InMoveInput);
	void ClearMoveInput();
	void HandleSprintStarted();
	void HandleSprintStopped();
	bool CanStartJumpForAnimation() const;
	bool ConsumeRealLandingEventRequested();
	void HandleAnimationEvent(EProject_JLocomotionAnimEvent EventType);

protected:
	void CacheOwnerReferences();
	AProject_JPlayerCharacter* GetPlayerOwner() const;
	UCharacterMovementComponent* GetCachedMovementComponent() const;
	UAbilitySystemComponent* GetCachedAbilitySystemComponent() const;
	bool IsInAirForAnimation() const;
	bool ShouldUseLocalInputState() const;
	bool IsDedicatedServerContext() const;
	bool WasRecentlyRendered() const;
	bool IsSprintRequestedForAnimation() const;
	bool IsRemoteInAirForAnimation(bool bMovementReportsInAir) const;
	bool IsRemoteGroundedByProbe() const;
	FVector2D GetMovementInputForState() const;
	void UpdateLocalAirState(bool bIsCurrentlyInAir);
	void UpdateRemoteAirState(float DeltaTime, bool bIsCurrentlyInAir);
	void StartLanding(float ImpactFallSpeed, bool bBroadcastRealLandingEvent, bool bUpdateGameplayTags);
	void StartFallOffStart();
	void StopFallOffStart();
	bool IsLandingStateActive() const;
	void ClearLandingTimers();
	void FinishLandingImmediately();
	FVector CalculateMoveWorldDirection(const FVector2D& MoveInput) const;
	void OnLandingTimerFinished();
	void OnJumpTimerFinished();
	void OnFallOffStartFinished();
	void CompleteGroundStart();
	void CompleteStop();
	void CompleteJumpStart();
	void CompleteFallOffStart();
	void CompleteLanding();
	void EnterGroundMotionMode(EProject_JGroundMotionMode NewMode);
	void RefreshGroundMotionFlags();
	void UpdateMovementRequestState(float DeltaTime);
	void UpdateRemoteMovementRequestState(float DeltaTime);
	void UpdateGroundMotionModeFromInput(float DeltaTime, const FVector2D& MoveInput, bool bAllowSharpTurn);
	void UpdateCombatMovementState(const FVector& HorizontalVelocity);
	void ClearMovementRequests();
	void ClearTransientAnimationRequests();

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StandLandingRequestDuration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingMinHoldTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeavyLandSpeedThreshold = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RealLandingEventSpeedThreshold = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float JumpStartMaxDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float JumpStartMinHoldTime = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float JumpStartNotifyIgnoreTime = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IgnoreLandingAfterJumpStartTime = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float JumpStartGroundContactGraceTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float ReplicatedJumpStartDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float FallOffStartDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoveInputDeadZone = 0.1f;

	/** Fallback only. GroundStartFinished notify should normally leave Start before this timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartFallbackDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	/** Fallback only. StopFinished notify should normally leave Stop before this timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StopFallbackDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLocomotionSpeedThreshold = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Finished", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FinishedExitWindow = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnMinSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLandingTurnCancelAngle = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLandingTurnCancelMinTime = 0.05f;

	/** Remote proxies do not have authoritative local input, so derive request state from replicated movement instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bUseInputDerivedRequestsForRemotePlayers = false;

	/** Minimum replicated movement speed treated as movement input for remote proxies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteMoveSpeedThreshold = 3.0f;

	/** Corrects simulated proxies that report Falling while their capsule is still on/near walkable ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bUseRemoteGroundProbe = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteGroundProbeDistance = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteGroundedVerticalSpeedTolerance = 20.0f;

	/** Remote landing can be missed if replicated Falling lasts for only a tiny window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteLandingMinAirTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteLandingMinFallSpeed = 120.0f;

	/** Hidden simulated proxies can update animation-facing state less often. Visible and local players still update every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HiddenRemoteUpdateInterval = 0.0f;

	/** Dedicated servers do not need animation-only state polling every frame. Event handlers still run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bSkipDedicatedServerAnimStateUpdate = true;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bCanExitLanding = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Landing")
	bool bLandingFinished = true;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Ground")
	EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Ground")
	bool bUseGroundLocomotionState = false;

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
	bool bUseGroundLocomotionDatabase = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bUseSprintLocomotion = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bStopWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bUseStopDatabase = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bIsStopping = false;

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
	TObjectPtr<UCapsuleComponent> CachedCapsuleComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> CachedAbilitySystemComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMeshComponent = nullptr;

	FTimerHandle LandingTimerHandle;
	FTimerHandle JumpTimerHandle;
	FTimerHandle FallOffStartTimerHandle;
	FTimerHandle JumpStartExitTimerHandle;
	FTimerHandle FallOffStartExitTimerHandle;
	FTimerHandle LandingExitTimerHandle;

	FVector2D CachedMoveInput = FVector2D::ZeroVector;
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;
	FVector InitialLandingMoveWorldDirection = FVector::ZeroVector;
	FVector PreviousLandingMoveWorldDirection = FVector::ZeroVector;
	bool bWasInAir = false;
	bool bPendingStartRequest = false;
	bool bPendingStopRequest = false;
	bool bSuppressFallOffStart = false;
	bool bRealLandingEventRequested = false;
	bool bSprintInputHeld = false;
	float GroundMotionModeElapsedTime = 0.0f;
	float JumpStartElapsedTime = 0.0f;
	bool bIgnoreNextLandingForJumpStart = false;
	bool bResolvedMoveInputLastUpdate = false;
	float HiddenRemoteUpdateAccumulator = 0.0f;
	float RemoteAirborneTime = 0.0f;
	float LandingElapsedTime = 0.0f;
	float StopElapsedTime = 0.0f;
	bool bPendingGroundStartFinish = false;
	bool bGroundStartFinishPendingExit = false;
	bool bStopFinishPendingExit = false;
	bool bJumpStartFinishPendingExit = false;
	bool bFallOffStartFinishPendingExit = false;
	bool bLandingFinishPendingExit = false;
};
