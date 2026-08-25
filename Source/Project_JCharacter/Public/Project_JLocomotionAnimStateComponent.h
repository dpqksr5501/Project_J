// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JReplicatedJumpState.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JLocomotionAnimStateComponent.generated.h"

class AProject_JPlayerCharacter;
struct FProject_JLocomotionTransitionPolicy;
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

	/** Velocity derivative in world space. Unlike CharacterMovement acceleration this remains meaningful while braking. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector VelocityAcceleration = FVector::ZeroVector;

	/** Signed, actor-local acceleration normalized to [-1, 1] by MaxAcceleration/BrakingDeceleration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector RelativeAccelerationAmount = FVector::ZeroVector;

	/** Angle from the current horizontal velocity to the requested future input direction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float VelocityToMoveInputAngle = 0.0f;

	/** Estimated distance required to stop at the current speed using CMC braking deceleration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float PredictedStopDistance = 0.0f;

	/** Gained horizontal speed over the short analysis horizon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float PredictedSpeedGain = 0.0f;

	/** Actual planar velocity reconstructed from the trajectory's present/future samples. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector FutureTrajectoryVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float FutureTrajectorySpeed = 0.0f;

	/** Absolute yaw delta between current velocity and the sampled future trajectory velocity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float FutureTrajectoryTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bHasFutureTrajectoryVelocity = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float DesiredFacingDeltaYaw = 0.0f;

	/** Absolute world-space facing yaw consumed by Animation Graph Steering targets. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float DesiredFacingYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsDecelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bHasPredictedMovement = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JDerivedLocomotionContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsMoving = false;

	/**
	 * Motion-Matching presentation movement state.  Unlike bIsMoving, Stop is
	 * deliberately non-moving so Cycle -> Stop is a core visual transition.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsMotionMatchingMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsStarting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	bool bIsPivoting = false;

	/** Monotonic local action-intent edge, kept separate from a consumed Pivot request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	int32 MoveIntentRevision = 0;

	/** Monotonic local presentation edge. A direct Pivot consumes this once. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	int32 PivotRequestRevision = 0;

	/** Physical travel direction captured before the reversal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector PivotPreviousMovementDirection = FVector::ZeroVector;

	/** Local movement intent captured at the same reversal edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion|Context")
	FVector PivotMoveIntentDirection = FVector::ZeroVector;

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
	void ApplyTransitionPolicy(const FProject_JLocomotionTransitionPolicy& InPolicy);
	void HandleJumpStarted();
	void HandleConfirmedRemoteJump(int32 Sequence, float ServerStartAgeSeconds, const FVector& LaunchVelocity);
	void HandleReplicatedFallOffStarted();
	void HandleReplicatedMoveStarted(bool bWasSprintingForStart);
	void HandleReplicatedMoveStopped(bool bWasSprintingAtStop);
	void HandleReplicatedLandingStarted(
		int32 Sequence,
		float ServerStartAgeSeconds,
		float ImpactFallSpeed,
		bool bWasMoving,
		bool bWasSprinting,
		bool bWasHeavy);
	void HandleReplicatedLandingCancelled(int32 Sequence);
	void HandleLanded(const FHitResult& Hit);
	void FinishLanding(bool bForceFinish = false);
	void SetMoveInput(const FVector2D& InMoveInput);
	void ClearMoveInput();
	/**
	 * Records a completed Enhanced Input semantic-direction snapshot for cosmetic
	 * Pivot selection. It never changes CharacterMovement or replication.
	 */
	void SetSemanticMoveIntentInput(const FVector2D& InMoveIntent, bool bHasActiveIntent);
	/** Marks an in-progress semantic chord update so raw IA_Move cannot consume an intermediate direction. */
	void BeginSemanticMoveIntentUpdate();
	void HandleSprintStarted();
	void HandleSprintStopped();
	bool CanStartJumpForAnimation() const;
	bool ConsumeRealLandingEventRequested();

	/** Complete value-only database-selection contract authored on the game thread. */
	const FProject_JMotionMatchingSelectionContext& GetMotionMatchingSelectionContext() const { return MotionMatchingSelectionContext; }

	UFUNCTION(BlueprintPure, Category = "Movement|Debug")
	FString GetDebugSummary() const;

	/** Seconds spent in the current semantic ground state; copied into the animation debug snapshot. */
	float GetGroundMotionModeElapsedTime() const { return GroundMotionModeElapsedTime; }

	UFUNCTION(BlueprintCallable, Category = "Movement|Debug")
	void ResetJumpStartLandingDebugState();

private:
	bool RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner);
	bool ShouldSkipUpdateForCurrentContext(float DeltaTime);
	void UpdateAirAndMovementRequests(float DeltaTime, bool bMovementReportsInAir);
	void UpdateLocomotionContexts(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot);
	void UpdateMotionMatchingSelectionState(const AProject_JPlayerCharacter& PlayerOwner);
	void LogMotionMatchingNetworkDebugIfEnabled(const AProject_JPlayerCharacter& PlayerOwner) const;
	FProject_JLocomotionAuthoritativeContext BuildAuthoritativeContext(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot) const;
	FProject_JLocomotionKinematicContext BuildKinematicContext(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot, float DeltaTime);
	FProject_JDerivedLocomotionContext BuildDerivedLocomotionContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext);
	void ApplyLocomotionPhaseStability(float DeltaTime, FProject_JDerivedLocomotionContext& InOutContext);
	EProject_JLocomotionGaitIntent ResolveGaitIntent(const AProject_JPlayerCharacter& PlayerOwner, const FProject_JLocomotionRuntimeSnapshot& Snapshot) const;
	EProject_JLocomotionRotationMode ResolveRotationMode(const AProject_JPlayerCharacter& PlayerOwner) const;
	EProject_JLocomotionPhaseFamily ResolvePhaseFamily(const FProject_JDerivedLocomotionContext& DerivedContext) const;
	bool IsMovingForContext(const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool IsMotionMatchingMovingForContext(const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool IsStartingForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool IsPivotingForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext);
	bool ShouldTurnInPlaceForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool ShouldSpinTransitionForContext(const FProject_JLocomotionAuthoritativeContext& AuthContext, const FProject_JLocomotionKinematicContext& KinematicContext) const;
	bool ShouldUseLocalInputState() const;
	bool IsSprintRequestedForAnimation() const;
	FProject_JLocomotionRuntimeSnapshot BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const;
	void ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot);

	void ClearRemoteMoveStartTransientState();
	bool TryFinishLandingForReplicatedMoveStart(bool bWasSprintingForStart);
	bool TryPromoteReplicatedStartToLocomotion();
	bool ShouldIgnoreRedundantReplicatedMoveStart() const;
	void QueueReplicatedMoveStart(bool bWasSprintingForStart);
	void QueueReplicatedMoveStop(bool bWasSprintingAtStop);
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
	void UpdateRemoteAirState(float DeltaTime, bool bIsCurrentlyInAir, bool bMovementReportsInAir);
	bool TryPredictRemoteJumpStart(bool bMovementReportsInAir);
	bool HasRemoteAirborneEvidence(bool bWasRemoteInAir) const;
	void UpdateRemoteAirborneEvidence(float DeltaTime);
	void ClearRemoteGroundedAirState();
	bool UpdateRemoteJumpStartState(float DeltaTime, bool bIsCurrentlyInAir, bool bHadRemoteAirborneEvidence);

	void BeginJumpStartState();
	void ScheduleJumpStartTimeout(float Duration);
	void BeginLandingState(const AProject_JPlayerCharacter& PlayerOwner, float ImpactFallSpeed);
	void ApplyReplicatedLandingSemantics(
		float ServerStartAgeSeconds,
		float ImpactFallSpeed,
		bool bWasMoving,
		bool bWasSprinting,
		bool bWasHeavy);
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
	void UpdateLocalMoveIntentSnapshot(const FVector2D& MoveInput);
	bool TryFinishLandingFromMovementInput(const FVector2D& MoveInput, bool bAllowSprintTurnCancel);
	bool TryFinishLandingFromInputChange();
	bool TryFinishLandingRedirectCancel(const FVector2D& MoveInput);
	bool TryFinishSprintLandingTurnCancel(const FVector2D& MoveInput);
	bool HasLandingDirectionTurnCancel(const FVector2D& MoveInput, float AngleThreshold);
	bool HasLandingActorTurnCancel(float AngleThreshold);
	void UpdateSharpTurnRequest(bool bAllowSharpTurn);
	bool ShouldInterruptStartForResponsiveTurn(const FVector2D& MoveInput, bool bAllowLocalControlYaw) const;
	bool HasLocalStartResponsiveTurn(float AngleThreshold) const;
	bool UpdateRemoteStartTurnExitRequest(const AProject_JPlayerCharacter& PlayerOwner, const FVector2D& MoveInput);
	void UpdateStartGroundMotionMode(const FVector2D& MoveInput, bool bAllowSharpTurn);
	void UpdateStopGroundMotionMode();
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
	void ClearPendingAnimationExitRequests();
	void ClearResolvedMoveInputState();

public:
	/**
	 * Maximum time a moving landing may own the Motion Matching database. The
	 * Motion Matching search policy holds the selected one-shot landing pose during
	 * this interval, so this is a recovery-state duration rather than a reselect cadence.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 1.00f;

	/** Maximum time a stand landing may own the Motion Matching database. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StandLandingRequestDuration = 1.00f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingMinHoldTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingInputCancelGraceTime = 0.25f;

	/** Minimum post-touchdown input time required before releasing input may hand a moving landing off to Stop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingExitStopInputHoldTime = 0.08f;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping|Network")
	FProject_JRemoteJumpPredictionPolicy RemoteJumpPredictionPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Jumping", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float FallOffStartDuration = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Input", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoveInputDeadZone = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopExitSpeedThreshold = 20.0f;

	/** Start hands off to Cycle after this fraction of CharacterMovement's actual target speed, not an authored fixed duration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Start", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float StartCompletionSpeedFraction = 0.90f;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLocomotionSpeedThreshold = 600.0f;

	/** Preserves Sprint Stop when sprint and movement input release in adjacent input frames. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float SprintStopMemoryDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Finished", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FinishedExitWindow = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnMinSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartTurnExitAngle = 15.0f;

	/** Sharper turn threshold that exits Start directly into locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Ground|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartResponsiveTurnExitAngle = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedStartInputHoldWindow = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedMovingSpeedThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedStartSpeedGainThreshold = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float DerivedMovementPredictionTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedStartMaxGroundSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedPivotAngleThreshold = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnAngleThreshold = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnMinSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedPivotMinSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DerivedTurnMinHoldTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TurnRedirectReselectCooldown = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Derived Context", meta = (ClampMin = "0.0", UIMin = "0.0"))
	/** GASP's actual ShouldTurnInPlace node uses 30 degrees; authored TIP rows then choose 90/180 assets. */
	float DerivedTurnInPlaceAngleThreshold = 30.0f;

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

	/** A remote MoveStop can arrive before the final replicated velocity update. Keep Cycle active above this speed so a Stop PSD is not searched against stale full-speed trajectory samples. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteStopEntryMaxSpeed = 180.0f;

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

	/** Monotonic version of the C++ locomotion state consumed by Motion Matching. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Selection")
	int32 MotionMatchingSelectionRevision = 1;

	/** Monotonic pulse consumed by the AnimInstance to break a held remote Start. */
	int32 StartResponsiveExitRevision = 0;

	/**
	 * Monotonic physical landing boundary consumed by the presentation layer.
	 * A bool landing state cannot distinguish two consecutive landings that reach
	 * the AnimInstance before the previous direct Blend Stack entry is forgotten.
	 */
	int32 LandingPresentationRevision = 0;

	/** True only for the update in which a database-selection input changed. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Selection")
	bool bMotionMatchingSelectionChanged = true;

	/** Explicit same-database re-search request, currently used by local combat-strafe input redirects. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Selection")
	bool bForceMotionMatchingReselect = false;

	/** The complete selection input consumed by the animation snapshot and database resolver. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Selection")
	FProject_JMotionMatchingSelectionContext MotionMatchingSelectionContext;

	/** Profile policy copied here so AnimInstance does not query remote selection policy directly. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Selection")
	bool bUseForwardOnlyRemoteStart = true;

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

	FVector2D CachedMoveInput = FVector2D::ZeroVector;
	FVector2D CachedSemanticMoveIntentInput = FVector2D::ZeroVector;
	FVector2D PreviousMoveInputForTurn = FVector2D::ZeroVector;
	FVector2D LastStableMoveInputDirection = FVector2D::ZeroVector;
	bool bHasLastStableMoveInputDirection = false;
	/** Previous stable local intent, retained only to gate Combat-Strafe Pivot to cardinal -> cardinal changes. */
	FVector2D PreviousStableMoveInputDirection = FVector2D::ZeroVector;
	bool bHasPreviousStableMoveInputDirection = false;
	bool bHasSemanticMoveIntentInput = false;
	bool bSemanticMoveIntentUpdatePending = false;
	/** One semantic-chord edge may be committed after CharacterMovement begins braking. */
	bool bHasSemanticPivotKinematicCapture = false;
	int32 SemanticPivotKinematicCaptureIntentRevision = INDEX_NONE;
	FVector SemanticPivotKinematicCapturePreviousDirection = FVector::ZeroVector;
	float SemanticPivotKinematicCaptureGroundSpeed = 0.0f;
	int32 MoveIntentRevision = 0;
	int32 LastConsumedPivotMoveIntentRevision = INDEX_NONE;
	int32 LastLoggedPivotRejectionMoveIntentRevision = INDEX_NONE;
	int32 PivotRequestRevision = 0;
	FVector LatchedPivotPreviousMovementDirection = FVector::ZeroVector;
	FVector LatchedPivotMoveIntentDirection = FVector::ZeroVector;
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
	int32 LastConfirmedRemoteJumpSequence = 0;
	bool bPredictedRemoteJumpStart = false;
	float RemoteStopStartSuppressTimeRemaining = 0.0f;
	FVector RemoteStartPreviousMoveWorldDirection = FVector::ZeroVector;
	float RemoteStartPreviousActorYaw = 0.0f;
	float StartPreviousControlYaw = 0.0f;
	float LandingElapsedTime = 0.0f;
	float LandingPostTouchdownMoveInputTime = 0.0f;
	float SprintStopMemoryTimeRemaining = 0.0f;
	double LastCombatStrafeReselectTimeSeconds = -DBL_MAX;
	bool bLandingFinishPendingExit = false;
	bool bForceLandingFinishToLocomotion = false;
	/** Latched only when movement genuinely continued after touchdown then released during a moving landing. */
	bool bLandingReceivedPostTouchdownMoveInput = false;
	bool bForceLandingFinishToStop = false;
	bool bLandingExitStopWasSprinting = false;
	bool bRemoteMoveReleasedWhileAirborne = false;
	/** A replicated MoveStop owns remote visual intent until a later MoveStart; residual network velocity must not restart locomotion. */
	bool bRemoteStopVisualIntentActive = false;
	bool bHasRemoteStartTurnReference = false;
	bool bLandingIgnoresRemoteGroundSpeed = false;
	bool bHasReplicatedStartGait = false;
	bool bReplicatedStartWasSprinting = false;
	bool bHasReplicatedStopGait = false;
	bool bReplicatedStopWasSprinting = false;
	int32 LastConfirmedRemoteLandingSequence = 0;
	bool bLandingCancelEventDispatched = false;
	bool bAppliedInAirGameplayTag = false;
	bool bAppliedLandingGameplayTag = false;
	bool bHasPublishedMotionMatchingSelection = false;
	bool bLastPublishedUseRemoteStart = false;
	EProject_JLocomotionGaitIntent LastPublishedMotionMatchingGait = EProject_JLocomotionGaitIntent::Run;
	EProject_JLocomotionRotationMode LastPublishedMotionMatchingRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionPhaseFamily LastPublishedMotionMatchingPhase = EProject_JLocomotionPhaseFamily::Idle;
	EProject_JGroundMotionMode LastPublishedGroundMotionMode = EProject_JGroundMotionMode::Idle;
	bool bLastPublishedHeavyLand = false;
	bool bLastPublishedLandWasMoving = false;
	bool bLastPublishedLandWasSprinting = false;
	bool bLastPublishedFallOffStart = false;
	FVector PreviousKinematicHorizontalVelocity = FVector::ZeroVector;
	bool bHasPreviousKinematicVelocity = false;
};
