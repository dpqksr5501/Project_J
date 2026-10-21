
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

UCLASS(BlueprintType, Blueprintable, ClassGroup=(Character), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JLocomotionAnimStateComponent : public UProject_JLocomotionAnimStateComponentBase
{
	GENERATED_BODY()
	friend class FProjectJLocomotionTagOwnershipTest;
	friend class FProjectJAudit2RemoteJumpTest;

public:
	UProject_JLocomotionAnimStateComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	void UpdateState(float DeltaTime);
	void ApplyTransitionPolicy(const FProject_JLocomotionTransitionPolicy& InPolicy);
	void HandleJumpStarted();
	bool HandleConfirmedRemoteJump(int32 Sequence, float ServerStartAgeSeconds, const FVector& LaunchVelocity);
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
	FVector2D GetHeldLocalMoveInput() const { return GetLocalMovementInputForState(); }

	void SetSemanticMoveIntentInput(const FVector2D& InMoveIntent, bool bHasActiveIntent);
	void BeginSemanticMoveIntentUpdate();
	void HandleSprintStarted();
	void HandleSprintStopped();
	bool CanStartJumpForAnimation() const;
	bool ConsumeRealLandingEventRequested();

	UFUNCTION(BlueprintPure, Category = "Movement|Debug")
	FString GetDebugSummary() const;

	private:
	friend class FProjectJAudit3MovementOwnerTest;
	friend class AProject_JAuditNetworkController;
	virtual void OnCachedAbilitySystemChanged() override;
	bool RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner);
	bool ShouldSkipUpdateForCurrentContext(float& DeltaTime);
	void AdvanceStateClocks(float DeltaTime);
	friend class FProjectJLocomotionCadenceTest;

	bool ShouldUseLocalInputState() const;
	bool IsSprintRequestedForAnimation() const;
	FProject_JLocomotionRuntimeSnapshot BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const;
	void ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot);

	void MarkRemoteMoveReleasedIfAirborne();

	void ClearLocalMoveInputState();

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
	void StartLanding(float ImpactFallSpeed, bool bBroadcastRealLandingEvent, bool bUpdateGameplayTags, float FoleyEventAgeSeconds = 0.0f);
	void StartFallOffStart(bool bReplicateEvent = true);
	void StopFallOffStart();
	bool IsLandingStateActive() const;
	void ClearJumpStartTimers();
	void ClearFallOffStartTimers();
	void ClearLandingTimers();
	bool SchedulePendingExit(FTimerHandle& TimerHandle, bool& bPendingExit, void (UProject_JLocomotionAnimStateComponent::*Callback)(), float Delay);
	bool ScheduleLandingMinHoldRetry();
	void FinishLandingImmediately();

void OnLandingTimerFinished();
	void OnJumpTimerFinished();
	void OnFallOffStartFinished();
	void CompleteJumpStart();
	void CompleteFallOffStart();
	void CompleteLanding();

	void RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput);

void AddOwnedInAirGameplayTag();
	void RemoveOwnedInAirGameplayTag();
	void AddOwnedLandingGameplayTag();
	void RemoveOwnedLandingGameplayTag();
	void ClearOwnedMovementGameplayTags();
	void ReconcileMovementGameplayTag(const FGameplayTag& Tag, TWeakObjectPtr<UAbilitySystemComponent>& Lease, const bool& bRequested);
	void ReleaseMovementGameplayTag(const FGameplayTag& Tag, TWeakObjectPtr<UAbilitySystemComponent>& Lease);

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float LandingRequestDuration = 1.00f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float StandLandingRequestDuration = 1.00f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Landing", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LandingMinHoldTime = 0.25f;

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
	float IdleSpeedThreshold = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Finished", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FinishedExitWindow = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bUseInputDerivedRequestsForRemotePlayers = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteMoveSpeedThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bUseRemoteGroundProbe = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteGroundProbeDistance = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteGroundedVerticalSpeedTolerance = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteLandingMinAirTime = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteLandingMinFallSpeed = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HiddenRemoteUpdateInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Network")
	bool bSkipDedicatedServerAnimStateUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optimization|Visibility", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RecentlyRenderedTolerance = 0.25f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bUsingLocalInputState = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bRecentlyRendered = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Optimization|State")
	bool bDedicatedServerContext = false;

	int32 LandingPresentationRevision = 0;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Input")
	bool bPrevHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bUseSprintLocomotion = false;

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
bool bHasSemanticMoveIntentInput = false;
	bool bSemanticMoveIntentUpdatePending = false;
bool bWasInAir = false;
	bool bSuppressFallOffStart = false;
	bool bRealLandingEventRequested = false;
	bool bSprintInputHeld = false;
float JumpStartElapsedTime = 0.0f;
	float JumpStartTimeoutElapsedTime = 0.0f;
	bool bIgnoreNextLandingForJumpStart = false;
	bool bResolvedMoveInputLastUpdate = false;
	float HiddenRemoteUpdateAccumulator = 0.0f;
	float RemoteAirborneTime = 0.0f;
	int32 LastConfirmedRemoteJumpSequence = 0;
	bool bPredictedRemoteJumpStart = false;
	float LandingElapsedTime = 0.0f;
bool bLandingFinishPendingExit = false;
bool bRemoteMoveReleasedWhileAirborne = false;
	bool bLandingIgnoresRemoteGroundSpeed = false;
	int32 LastConfirmedRemoteLandingSequence = 0;
	bool bLandingCancelEventDispatched = false;
	TWeakObjectPtr<UAbilitySystemComponent> OwnedInAirTagASC;
	TWeakObjectPtr<UAbilitySystemComponent> OwnedLandingTagASC;
	bool bInAirGameplayTagRequested = false;
	bool bLandingGameplayTagRequested = false;
	bool bMovementTagOwnershipEnding = false;

};
