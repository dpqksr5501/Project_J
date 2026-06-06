// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JLocomotionAnimStateComponent.generated.h"

class AProject_JPlayerCharacter;
struct FHitResult;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionRuntimeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	FVector HorizontalVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bHasSprintMovementIntent = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionAuthoritativeContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bSprintAllowed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bJumpAllowed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bCombatMode = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionKinematicContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector HorizontalVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector MoveWorldDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float AccelerationRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float DesiredFacingDeltaYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsAccelerating = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JDerivedLocomotionContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsStarting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsPivoting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bShouldTurnInPlace = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bShouldSpinTransition = false;
};

/**
 * Owns the player locomotion state consumed by animation graphs and Chooser Tables.
 *
 * This component owns the state-machine logic so MMORPG-scale character code can
 * keep animation-facing locomotion separate from player input and combat flow.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Character), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JLocomotionAnimStateComponent : public UProject_JLocomotionAnimStateComponentBase
{
	GENERATED_BODY()

public:
	UProject_JLocomotionAnimStateComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void UpdateState(float DeltaTime);
	void HandleJumpStarted();
	void HandleReplicatedJumpStarted();
	void HandleReplicatedFallOffStarted();
	void HandleReplicatedMoveStarted(bool bWasSprintingForStart);
	void HandleReplicatedMoveStopped();
	void HandleReplicatedLandingCancelled();
	void HandleLanded(const FHitResult& Hit);
	void FinishLanding(bool bForceFinish = false);
	void SetMoveInput(const FVector2D& InMoveInput);
	void ClearMoveInput();
	void HandleSprintStarted();
	void HandleSprintStopped();
	bool CanStartJumpForAnimation() const;
	bool ConsumeRealLandingEventRequested();

	UFUNCTION(BlueprintPure, Category = "Movement|Debug")
	FString GetDebugSummary() const;

	UFUNCTION(BlueprintCallable, Category = "Movement|Debug")
	void ResetJumpStartLandingDebugState();

private:
	bool RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner);
	bool ShouldSkipUpdateForCurrentContext(float DeltaTime);
	void UpdateAirAndMovementRequests(float DeltaTime, bool bMovementReportsInAir);
	void UpdateLocomotionContexts(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot);
	FProject_JLocomotionAuthoritativeContext BuildAuthoritativeContext(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot) const;
	FProject_JLocomotionKinematicContext BuildKinematicContext(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot) const;
	FProject_JDerivedLocomotionContext BuildDerivedLocomotionContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	void ApplyLocomotionPhaseStability(float DeltaTime, FProject_JDerivedLocomotionContext& InOutContext);
	EProject_JLocomotionGaitIntent ResolveGaitIntent(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot) const;
	EProject_JLocomotionRotationMode ResolveRotationMode(const AProject_JPlayerCharacter& PlayerOwner) const;
	EProject_JLocomotionPhaseFamily ResolvePhaseFamily(const FProject_JDerivedLocomotionContext& DerivedContext) const;
	bool IsMovingForContext(const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool IsStartingForContext(const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool IsPivotingForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool ShouldTurnInPlaceForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool ShouldSpinTransitionForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool ShouldUseLocalInputState() const;
	bool IsSprintRequestedForAnimation() const;
	FProject_JLocomotionRuntimeSnapshot BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const;
	void ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot);

	void ClearRemoteMoveStartTransientState();
	bool TryFinishLandingForReplicatedMoveStart(bool bWasSprintingForStart);
	bool TryPromoteReplicatedStartToLocomotion();
	void QueueReplicatedMoveStart(bool bWasSprintingForStart);
	bool TryPromoteReplicatedStopToLocomotion();
	void QueueReplicatedMoveStop();
	void MarkRemoteMoveReleasedIfAirborne();
	void TryFinishLandingForReplicatedMoveStop();

	bool HasAnyMoveInputState() const;
	bool HasCachedMoveInput() const;
	void QueueLocalMoveStartIfNeeded(bool bHadMoveInput, bool bHasNewMoveInput);
	void ClearLocalMoveInputState();
	void QueueLocalMoveStopIfNeeded(bool bHadMoveInput);
	void UpdateSprintLocomotionRequest();
	EProject_JGroundMotionMode ResolveGroundMotionModeAfterSprintStop() const;
	void TryFinishSprintLandingAfterSprintStop();

	bool HasRealFallingEvidenceForLanding(const AProject_JPlayerCharacter& PlayerOwner) const;
	bool ShouldIgnoreJumpStartLanding(const AProject_JPlayerCharacter& PlayerOwner) const;
	void RecordIgnoredJumpStartLanding(const AProject_JPlayerCharacter& PlayerOwner);
	void KeepJumpStartAirborneAfterIgnoredLanding();
	bool IsRemoteInAirForAnimation(bool bMovementReportsInAir) const;
	bool IsRemoteGroundedByProbe() const;
	FVector2D GetMovementInputForState() const;
	FVector2D GetLocalMovementInputForState() const;
	FVector2D GetRemoteMovementInputForState() const;
	void UpdateLocalAirState(bool bIsCurrentlyInAir);
	void UpdateLocalAirborneEvidence(bool bIsCurrentlyInAir);
	bool TryStartLocalLandingFromJump(bool bIsCurrentlyInAir);
	bool TryStartLocalFallOff(bool bIsCurrentlyInAir);
	bool TryClearLocalGroundedAirState(bool bIsCurrentlyInAir);
	void RefreshLocalAirEntryFlags(bool bIsCurrentlyInAir);
	void UpdateRemoteAirState(float DeltaTime, bool bIsCurrentlyInAir);
	bool HasRemoteAirborneEvidence(bool bWasRemoteInAir) const;
	void UpdateRemoteAirborneEvidence(float DeltaTime);
	void ClearRemoteGroundedAirState();
	bool UpdateRemoteJumpStartState(float DeltaTime, bool bIsCurrentlyInAir, bool bHadRemoteAirborneEvidence);

	void BeginJumpStartState();
	void ScheduleJumpStartTimeout(float Duration);
	void BeginLandingState(const AProject_JPlayerCharacter& PlayerOwner, float ImpactFallSpeed);
	void ScheduleLandingTimeout();
	void ClearActiveLandingState();
	void StartLanding(float ImpactFallSpeed, bool bBroadcastRealLandingEvent, bool bUpdateGameplayTags);
	void StartFallOffStart(bool bReplicateEvent = true);
	void StopFallOffStart();
	bool IsLandingStateActive() const;
	void ClearJumpStartTimers();
	void ClearFallOffStartTimers();
	void ClearLandingTimers();
	bool SchedulePendingExit(FTimerHandle& TimerHandle, bool& bPendingExit, void (UProject_JLocomotionAnimStateComponent::*Callback)(), float Delay);
	bool ScheduleLandingMinHoldRetry();
	void FinishLandingImmediately();
	void DispatchLandingCancelForAnimation();
	FVector CalculateMoveWorldDirection(const FVector2D& MoveInput) const;
	void OnLandingTimerFinished();
	void OnJumpTimerFinished();
	void OnFallOffStartFinished();
	void CompleteJumpStart();
	void CompleteFallOffStart();
	void CompleteLanding();

	void EnterGroundMotionMode(EProject_JGroundMotionMode NewMode);
	void ResetGroundMotionTransitionRequests();
	void HandleGroundMotionModeEntered(EProject_JGroundMotionMode NewMode);
	void EnterStartGroundMotionMode();
	void EnterStopGroundMotionMode();
	void ClearGroundMotionSprintTransitionState();
	void CacheRemoteStartTurnReference();
	void ClearRemoteStartTurnReference();
	bool UpdateLocalStartTurnExitRequest();
	void RefreshGroundMotionFlags();
	void UpdateMovementRequestState(float DeltaTime);
	void UpdateRemoteMovementRequestState(float DeltaTime);
	bool ConsumeRemoteStopStartSuppress(float DeltaTime);
	void ApplyRemoteStopStartSuppress();
	void RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput, bool bTrackTurnAngle);
	bool TryFinishLandingFromMovementInput(const FVector2D& MoveInput, bool bAllowSprintTurnCancel);
	bool TryFinishLandingFromInputChange();
	bool TryFinishLandingRedirectCancel(const FVector2D& MoveInput);
	bool TryFinishSprintLandingTurnCancel(const FVector2D& MoveInput);
	bool HasLandingDirectionTurnCancel(const FVector2D& MoveInput, float AngleThreshold);
	bool HasLandingActorTurnCancel(float AngleThreshold);
	void UpdateSharpTurnRequest(bool bAllowSharpTurn);
	bool ShouldInterruptStartForResponsiveTurn(const FVector2D& MoveInput, bool bAllowLocalControlYaw) const;
	bool HasLocalStartResponsiveTurn(float AngleThreshold) const;
	void ScheduleStartAutoPromote();
	void ClearStartAutoPromoteTimer();
	void PromoteStartToResolvedGroundMotion();
	bool UpdateRemoteStartTurnExitRequest(const AProject_JPlayerCharacter& PlayerOwner, const FVector2D& MoveInput);
	void UpdateStartGroundMotionMode(const FVector2D& MoveInput, bool bAllowSharpTurn);
	void UpdateStopGroundMotionMode(float DeltaTime);
	void UpdateDefaultGroundMotionMode();
	void UpdateGroundMotionModeFromInput(float DeltaTime, const FVector2D& MoveInput, bool bAllowSharpTurn);
	bool CanRequestGroundMotion() const;
	void ClearGroundMotionInputRequests(const FVector2D& MoveInput);

	void UpdateCombatMovementState(const FVector& HorizontalVelocity);
	void ClearCombatMovementState();
	void UpdateLocalCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner);
	void UpdateRemoteCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner, const FVector& HorizontalVelocity);

	void AddOwnedInAirGameplayTag();
	void RemoveOwnedInAirGameplayTag();
	void AddOwnedLandingGameplayTag();
	void RemoveOwnedLandingGameplayTag();
	void ClearOwnedMovementGameplayTags();
	void ClearMovementRequests();
	void ClearTransientAnimationRequests();
	void ClearPendingAnimationExitRequests();
	void ClearResolvedMoveInputState();

public:
	/** Maximum time a moving landing can hold without a cancel or new phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 2.0f;

	/** Maximum time a stand landing can hold without a cancel or new phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StandLandingRequestDuration = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingMinHoldTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingInputCancelGraceTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HeavyLandSpeedThreshold = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RealLandingEventSpeedThreshold = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float JumpStartMaxDuration = 2.0f;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartMinDuration = 1.4f;

	/** Maximum time Start can hold without reaching locomotion. Start no longer depends on GroundStartFinished notify. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StartMaxDuration = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopMinDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopExitSpeedThreshold = 20.0f;

	/** Maximum time Stop can hold without a new movement input. Stop no longer depends on StopFinished notify. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StopFallbackDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLocomotionSpeedThreshold = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Finished", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FinishedExitWindow = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnMinSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartTurnExitAngle = 15.0f;

	/** Start can be interrupted after this short window when input/camera/replicated movement turns sharply. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartResponsiveTurnExitMinTime = 0.08f;

	/** Sharper turn threshold that bypasses StartMinDuration and moves directly into locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartResponsiveTurnExitAngle = 35.0f;

	/** Start can exit into Stop/Idle after this short window if movement input is released quickly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Stop", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartInputReleaseExitMinTime = 0.08f;

	/** Failsafe that prevents Start/RemoteStart PSD from persisting if update throttling or remote input inference misses the exit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StartAutoPromoteDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedStartInputHoldWindow = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedStartMaxGroundSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedPivotAngleThreshold = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnAngleThreshold = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnMinHoldTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnInPlaceAngleThreshold = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedSpinTransitionAngleThreshold = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingRedirectCancelAngle = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingRedirectCancelMinTime = 0.05f;

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

	/** Prevents residual replicated velocity from creating a fake remote Start right after an explicit remote Stop event. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteStopStartSuppressDuration = 0.20f;

	/** Simulated proxies leave Start immediately when replicated movement direction changes this much. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteStartTurnExitAngle = 15.0f;

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

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Context")
	FProject_JLocomotionAuthoritativeContext AuthoritativeContext;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Context")
	FProject_JLocomotionKinematicContext KinematicContext;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Context")
	FProject_JDerivedLocomotionContext DerivedLocomotionContext;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Context")
	EProject_JLocomotionPhaseFamily PreviousDerivedPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Context")
	float DerivedPhaseFamilyElapsedTime = 0.0f;

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

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	int32 IgnoredJumpStartLandingCount = 0;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	float LastIgnoredJumpStartLandingElapsedTime = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	float LastIgnoredJumpStartLandingVelocityZ = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	float LastIgnoredJumpStartLandingFallSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	float LastIgnoredJumpStartLandingVerticalSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Debug")
	bool bLastIgnoredJumpStartLandingHadFallingEvidence = false;

private:
	FTimerHandle LandingTimerHandle;
	FTimerHandle JumpTimerHandle;
	FTimerHandle FallOffStartTimerHandle;
	FTimerHandle LandingExitTimerHandle;
	FTimerHandle StartAutoPromoteTimerHandle;

	FVector2D CachedMoveInput = FVector2D::ZeroVector;
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;
	FVector InitialLandingMoveWorldDirection = FVector::ZeroVector;
	FVector PreviousLandingMoveWorldDirection = FVector::ZeroVector;
	float InitialLandingActorYaw = 0.0f;
	float PreviousLandingActorYaw = 0.0f;
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
	float RemoteStopStartSuppressTimeRemaining = 0.0f;
	FVector RemoteStartPreviousMoveWorldDirection = FVector::ZeroVector;
	float RemoteStartPreviousActorYaw = 0.0f;
	float StartPreviousControlYaw = 0.0f;
	float LandingElapsedTime = 0.0f;
	float StopElapsedTime = 0.0f;
	bool bLandingFinishPendingExit = false;
	bool bForceLandingFinishToLocomotion = false;
	bool bRemoteMoveReleasedWhileAirborne = false;
	bool bHasRemoteStartTurnReference = false;
	bool bLandingIgnoresRemoteGroundSpeed = false;
	bool bLandingCancelEventDispatched = false;
	bool bAppliedInAirGameplayTag = false;
	bool bAppliedLandingGameplayTag = false;
};
