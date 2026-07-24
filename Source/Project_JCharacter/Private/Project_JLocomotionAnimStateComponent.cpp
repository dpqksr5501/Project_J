// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

UProject_JLocomotionAnimStateComponent::UProject_JLocomotionAnimStateComponent()
{
}

void UProject_JLocomotionAnimStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearStartAutoPromoteTimer();
	ClearOwnedMovementGameplayTags();
	Super::EndPlay(EndPlayReason);
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
	if (!IsRegistered())
	{
		return;
	}

	AProject_JPlayerCharacter* PlayerOwner = nullptr;
	if (!RefreshOwnerReferencesForUpdate(PlayerOwner))
	{
		return;
	}

	if (ShouldSkipUpdateForCurrentContext(DeltaTime))
	{
		return;
	}

	const FProject_JLocomotionRuntimeSnapshot MovementSnapshot = BuildMovementSnapshot(*PlayerOwner);
	ApplyMovementSnapshot(DeltaTime, MovementSnapshot);

	UpdateAirAndMovementRequests(DeltaTime, IsInAirForAnimation());
	UpdateLocomotionContexts(DeltaTime, MovementSnapshot);
	UpdateCombatMovementState(MovementSnapshot.HorizontalVelocity);
}

bool UProject_JLocomotionAnimStateComponent::RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner)
{
	if (!GetPlayerOwner() || !GetCachedMovementComponent())
	{
		CacheOwnerReferences();
	}

	OutPlayerOwner = GetPlayerOwner();
	return OutPlayerOwner != nullptr;
}

bool UProject_JLocomotionAnimStateComponent::ShouldSkipUpdateForCurrentContext(float DeltaTime)
{
	bUsingLocalInputState = ShouldUseLocalInputState();
	bDedicatedServerContext = IsDedicatedServerContext();
	if (bDedicatedServerContext && bSkipDedicatedServerAnimStateUpdate)
	{
		return true;
	}

	bRecentlyRendered = WasRecentlyRendered(RecentlyRenderedTolerance);
	if (!bUsingLocalInputState && !bRecentlyRendered)
	{
		const float UpdateInterval = HiddenRemoteUpdateInterval;
		if (UpdateInterval > 0.0f)
		{
			HiddenRemoteUpdateAccumulator += DeltaTime;
			if (HiddenRemoteUpdateAccumulator < UpdateInterval)
			{
				return true;
			}
		}
	}

	HiddenRemoteUpdateAccumulator = 0.0f;
	return false;
}

void UProject_JLocomotionAnimStateComponent::UpdateAirAndMovementRequests(float DeltaTime, bool bMovementReportsInAir)
{
	if (bUsingLocalInputState)
	{
		UpdateLocalAirState(bMovementReportsInAir);
		UpdateMovementRequestState(DeltaTime);
		return;
	}

	UpdateRemoteAirState(
		DeltaTime,
		IsRemoteInAirForAnimation(bMovementReportsInAir),
		bMovementReportsInAir);
	UpdateRemoteMovementRequestState(DeltaTime);
}

void UProject_JLocomotionAnimStateComponent::UpdateLocomotionContexts(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		AuthoritativeContext = FProject_JLocomotionAuthoritativeContext();
		KinematicContext = FProject_JLocomotionKinematicContext();
		DerivedLocomotionContext = FProject_JDerivedLocomotionContext();
		PreviousDerivedPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		DerivedPhaseFamilyElapsedTime = 0.0f;
		return;
	}

	AuthoritativeContext = BuildAuthoritativeContext(*PlayerOwner, Snapshot);
	KinematicContext = BuildKinematicContext(*PlayerOwner, Snapshot);
	FProject_JDerivedLocomotionContext NewDerivedContext = BuildDerivedLocomotionContext(AuthoritativeContext, KinematicContext);
	ApplyLocomotionPhaseStability(DeltaTime, NewDerivedContext);
	DerivedLocomotionContext = NewDerivedContext;
}

FProject_JLocomotionAuthoritativeContext UProject_JLocomotionAnimStateComponent::BuildAuthoritativeContext(
	const AProject_JPlayerCharacter& PlayerOwner,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot) const
{
	FProject_JLocomotionAuthoritativeContext Context;
	Context.bSprintAllowed = PlayerOwner.IsSprintLocomotionAllowed();
	Context.bJumpAllowed = PlayerOwner.IsJumpLocomotionAllowed();
	Context.bCombatMode = PlayerOwner.IsCombatModeActive();
	Context.GaitIntent = ResolveGaitIntent(PlayerOwner, Snapshot);
	Context.RotationMode = ResolveRotationMode(PlayerOwner);
	return Context;
}

FProject_JLocomotionKinematicContext UProject_JLocomotionAnimStateComponent::BuildKinematicContext(
	const AProject_JPlayerCharacter& PlayerOwner,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot) const
{
	FProject_JLocomotionKinematicContext Context;
	Context.Velocity = Snapshot.Velocity;
	Context.HorizontalVelocity = Snapshot.HorizontalVelocity;
	Context.GroundSpeed = Snapshot.GroundSpeed;
	Context.VerticalSpeed = Snapshot.VerticalSpeed;
	Context.MoveInputTurnAngle = MoveInputTurnAngle;
	Context.bHasMoveInput = bHasMoveInput;
	Context.MoveWorldDirection = CalculateMoveWorldDirection(GetMovementInputForState());

	if (const UCharacterMovementComponent* MovementComponent = PlayerOwner.GetCharacterMovement())
	{
		Context.Acceleration = MovementComponent->GetCurrentAcceleration();
		Context.bIsAccelerating = Context.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Context.AccelerationRatio = FMath::Clamp(Context.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
	}

	if (!Context.MoveWorldDirection.IsNearlyZero())
	{
		const float DesiredYaw = Context.MoveWorldDirection.Rotation().Yaw;
		Context.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(PlayerOwner.GetActorRotation().Yaw, DesiredYaw);
	}
	else
	{
		Context.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(
			PlayerOwner.GetActorRotation().Yaw,
			PlayerOwner.GetControlRotation().Yaw);
	}

	return Context;
}

FProject_JDerivedLocomotionContext UProject_JLocomotionAnimStateComponent::BuildDerivedLocomotionContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	FProject_JDerivedLocomotionContext Context;
	Context.bIsMoving = IsMovingForContext(InKinematicContext);
	Context.bIsStarting = IsStartingForContext(InKinematicContext);
	Context.bIsPivoting = IsPivotingForContext(AuthContext, InKinematicContext);
	Context.bShouldTurnInPlace = ShouldTurnInPlaceForContext(AuthContext, InKinematicContext);
	Context.bShouldSpinTransition = ShouldSpinTransitionForContext(AuthContext, InKinematicContext);
	Context.PhaseFamily = ResolvePhaseFamily(Context);
	return Context;
}

void UProject_JLocomotionAnimStateComponent::ApplyLocomotionPhaseStability(
	float DeltaTime,
	FProject_JDerivedLocomotionContext& InOutContext)
{
	if (PreviousDerivedPhaseFamily == InOutContext.PhaseFamily)
	{
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	const bool bKeepMovingTurn =
		PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::Turn &&
		InOutContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		DerivedPhaseFamilyElapsedTime < DerivedTurnMinHoldTime &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (bKeepMovingTurn)
	{
		InOutContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	PreviousDerivedPhaseFamily = InOutContext.PhaseFamily;
	DerivedPhaseFamilyElapsedTime = 0.0f;
}

EProject_JLocomotionGaitIntent UProject_JLocomotionAnimStateComponent::ResolveGaitIntent(
	const AProject_JPlayerCharacter&,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot) const
{
	if (bWantsSprint || Snapshot.GroundSpeed >= SprintLocomotionSpeedThreshold)
	{
		return EProject_JLocomotionGaitIntent::Sprint;
	}

	return Snapshot.GroundSpeed > IdleSpeedThreshold || bHasMoveInput
		? EProject_JLocomotionGaitIntent::Run
		: EProject_JLocomotionGaitIntent::Walk;
}

EProject_JLocomotionRotationMode UProject_JLocomotionAnimStateComponent::ResolveRotationMode(
	const AProject_JPlayerCharacter& PlayerOwner) const
{
	return PlayerOwner.IsCombatModeActive()
		? EProject_JLocomotionRotationMode::Strafe
		: EProject_JLocomotionRotationMode::OrientToMovement;
}

EProject_JLocomotionPhaseFamily UProject_JLocomotionAnimStateComponent::ResolvePhaseFamily(
	const FProject_JDerivedLocomotionContext& Context) const
{
	if (IsLandingStateActive())
	{
		return EProject_JLocomotionPhaseFamily::Landing;
	}
	if (bIsJumping)
	{
		return EProject_JLocomotionPhaseFamily::JumpStart;
	}
	if (bIsFallOffStart || bIsInAir)
	{
		return EProject_JLocomotionPhaseFamily::Fall;
	}
	if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		return EProject_JLocomotionPhaseFamily::Stop;
	}
	if (Context.bShouldTurnInPlace)
	{
		return EProject_JLocomotionPhaseFamily::TurnInPlace;
	}
	if (Context.bIsPivoting)
	{
		// A combat Strafe pivot uses the authored TurnRedirect database too. Keep
		// it in the Turn family so the short hold below can finish the redirect
		// before returning to the lateral Cycle search.
		return AuthoritativeContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
			? EProject_JLocomotionPhaseFamily::Turn
			: EProject_JLocomotionPhaseFamily::Pivot;
	}
	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		return EProject_JLocomotionPhaseFamily::Start;
	}
	if (GroundMotionMode != EProject_JGroundMotionMode::Locomotion && Context.bIsStarting)
	{
		return EProject_JLocomotionPhaseFamily::Start;
	}
	const bool bHasMovingTurnIntent =
		Context.bIsMoving &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed > StopIntentSpeedThreshold;
	const bool bIsCombatStrafe =
		AuthoritativeContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const bool bShouldUseTurnRedirect = bHasMovingTurnIntent &&
		(bIsCombatStrafe
			// In Strafe, actor facing intentionally remains camera-facing. A held A/D
			// input therefore has a permanent +/-90 desired-facing delta and must not
			// pin the character in TurnRedirect. Only an actual input-direction change
			// starts the redirect; phase stability keeps it briefly before Cycle.
			? FMath::Abs(KinematicContext.MoveInputTurnAngle) >= DerivedTurnAngleThreshold
			: FMath::Abs(KinematicContext.DesiredFacingDeltaYaw) >= DerivedTurnAngleThreshold);
	if (bShouldUseTurnRedirect)
	{
		return EProject_JLocomotionPhaseFamily::Turn;
	}

	return Context.bIsMoving ? EProject_JLocomotionPhaseFamily::Cycle : EProject_JLocomotionPhaseFamily::Idle;
}

bool UProject_JLocomotionAnimStateComponent::IsMovingForContext(const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	return InKinematicContext.bHasMoveInput || InKinematicContext.GroundSpeed > IdleSpeedThreshold;
}

bool UProject_JLocomotionAnimStateComponent::IsStartingForContext(const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	return
		InKinematicContext.bHasMoveInput &&
		MoveInputHeldTime <= DerivedStartInputHoldWindow &&
		InKinematicContext.GroundSpeed <= DerivedStartMaxGroundSpeed &&
		!bIsInAir &&
		!IsLandingStateActive();
}

bool UProject_JLocomotionAnimStateComponent::IsPivotingForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	if (AuthContext.RotationMode != EProject_JLocomotionRotationMode::Strafe)
	{
		return false;
	}

	if (!InKinematicContext.bHasMoveInput || InKinematicContext.GroundSpeed <= StopIntentSpeedThreshold)
	{
		return false;
	}

	const float PivotThreshold = DerivedPivotAngleThreshold * 0.75f;
	return FMath::Abs(InKinematicContext.MoveInputTurnAngle) >= PivotThreshold;
}

bool UProject_JLocomotionAnimStateComponent::ShouldTurnInPlaceForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	// A simulated proxy does not have the owning player's current controller
	// yaw. Treating its replicated/fallback control rotation as authoritative
	// produces a permanent facing delta and repeatedly selects a Turn PSD while
	// the remote character is actually stationary. Remote idle must remain Idle;
	// future replicated aim/lock-on state can opt into a dedicated remote turn.
	if (!ShouldUseLocalInputState())
	{
		return false;
	}

	return
		AuthContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		!InKinematicContext.bHasMoveInput &&
		InKinematicContext.GroundSpeed <= IdleSpeedThreshold &&
		FMath::Abs(InKinematicContext.DesiredFacingDeltaYaw) >= DerivedTurnInPlaceAngleThreshold &&
		!bIsInAir &&
		!IsLandingStateActive();
}

bool UProject_JLocomotionAnimStateComponent::ShouldSpinTransitionForContext(
	const FProject_JLocomotionAuthoritativeContext&,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	return
		InKinematicContext.bHasMoveInput &&
		InKinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		FMath::Abs(InKinematicContext.DesiredFacingDeltaYaw) >= DerivedSpinTransitionAngleThreshold;
}

FVector UProject_JLocomotionAnimStateComponent::CalculateMoveWorldDirection(const FVector2D& MoveInput) const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || MoveInput.SizeSquared() <= FMath::Square(MoveInputDeadZone))
	{
		return FVector::ZeroVector;
	}

	if (!ShouldUseLocalInputState())
	{
		FVector HorizontalVelocity = PlayerOwner->GetVelocity();
		HorizontalVelocity.Z = 0.0f;
		return HorizontalVelocity.GetSafeNormal();
	}

	const FRotator ControlYawRotation(0.0f, PlayerOwner->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);
	return (ForwardDirection * MoveInput.Y + RightDirection * MoveInput.X).GetSafeNormal();
}

bool UProject_JLocomotionAnimStateComponent::ShouldUseLocalInputState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return PlayerOwner && (PlayerOwner->IsLocallyControlled() || bUseInputDerivedRequestsForRemotePlayers);
}

bool UProject_JLocomotionAnimStateComponent::IsSprintRequestedForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return (bSprintInputHeld && (!PlayerOwner || PlayerOwner->IsSprintInputDirectionAllowed())) ||
		(PlayerOwner && PlayerOwner->IsSprintLocomotionAllowed());
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteInAirForAnimation(bool bMovementReportsInAir) const
{
	if (!bMovementReportsInAir)
	{
		return false;
	}

	if (FMath::Abs(VerticalSpeed) <= RemoteGroundedVerticalSpeedTolerance && IsRemoteGroundedByProbe())
	{
		return false;
	}

	return true;
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteGroundedByProbe() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const UWorld* World = GetWorld();
	const UCapsuleComponent* CapsuleComponent = GetCachedCapsuleComponent();
	if (!bUseRemoteGroundProbe || !PlayerOwner || !World || !CapsuleComponent)
	{
		return false;
	}

	const FVector Start = PlayerOwner->GetActorLocation();
	const float TraceDistance = CapsuleComponent->GetScaledCapsuleHalfHeight() + RemoteGroundProbeDistance;
	const FVector End = Start - FVector(0.0f, 0.0f, TraceDistance);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RemoteGroundProbe), false, PlayerOwner);
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit;
}

FVector2D UProject_JLocomotionAnimStateComponent::GetMovementInputForState() const
{
	return ShouldUseLocalInputState() ? GetLocalMovementInputForState() : GetRemoteMovementInputForState();
}

FVector2D UProject_JLocomotionAnimStateComponent::GetLocalMovementInputForState() const
{
	return CachedMoveInput.GetClampedToMaxSize(1.0f);
}

FVector2D UProject_JLocomotionAnimStateComponent::GetRemoteMovementInputForState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return FVector2D::ZeroVector;
	}

	FVector HorizontalVelocity = PlayerOwner->GetVelocity();
	HorizontalVelocity.Z = 0.0f;
	if (HorizontalVelocity.SizeSquared() <= FMath::Square(RemoteMoveSpeedThreshold))
	{
		return FVector2D::ZeroVector;
	}

	const FVector LocalVelocity = PlayerOwner->GetActorTransform().InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
	return FVector2D(LocalVelocity.Y, LocalVelocity.X).GetClampedToMaxSize(1.0f);
}

FProject_JLocomotionRuntimeSnapshot UProject_JLocomotionAnimStateComponent::BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const
{
	FProject_JLocomotionRuntimeSnapshot Snapshot;
	Snapshot.Velocity = PlayerOwner.GetVelocity();
	Snapshot.HorizontalVelocity = FVector(Snapshot.Velocity.X, Snapshot.Velocity.Y, 0.0f);
	Snapshot.VerticalSpeed = Snapshot.Velocity.Z;
	Snapshot.GroundSpeed = Snapshot.HorizontalVelocity.Size();
	Snapshot.bWantsSprint = IsSprintRequestedForAnimation();
	Snapshot.bHasSprintMovementIntent = CachedMoveInput.Size() > MoveInputDeadZone || Snapshot.GroundSpeed > IdleSpeedThreshold;
	return Snapshot;
}

void UProject_JLocomotionAnimStateComponent::ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot)
{
	VerticalSpeed = Snapshot.VerticalSpeed;
	GroundSpeed = Snapshot.GroundSpeed;
	bWantsSprint = Snapshot.bWantsSprint;
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		bWantsSprint &&
		Snapshot.bHasSprintMovementIntent;
	GroundMotionModeElapsedTime += DeltaTime;
	if (bIsJumping)
	{
		JumpStartElapsedTime += DeltaTime;
	}

	if (bIsLanding)
	{
		LandingElapsedTime += DeltaTime;
		bCanExitLanding = LandingElapsedTime >= LandingMinHoldTime;
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateMovementRequestState(float DeltaTime)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		ClearMovementRequests();
		return;
	}

	const FVector2D MoveInput = GetLocalMovementInputForState();
	RefreshMovementInputState(DeltaTime, MoveInput, true);
	if (TryFinishLandingFromMovementInput(MoveInput, true))
	{
		return;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, MoveInput, true);
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	const FVector2D MoveInput = GetRemoteMovementInputForState();
	const bool bSuppressStartFromResidualVelocity = ConsumeRemoteStopStartSuppress(DeltaTime);

	RefreshMovementInputState(DeltaTime, MoveInput, true);
	if (bSuppressStartFromResidualVelocity)
	{
		ApplyRemoteStopStartSuppress();
	}

	// CharacterMovement velocity on a simulated proxy decays several replicated
	// frames after the owner releases input. A confirmed MoveStop is more
	// authoritative for visual intent than that residual velocity; otherwise a
	// remote Stop enters for one frame and is immediately overwritten by Start.
	const FVector2D VisualMoveInput = bRemoteStopVisualIntentActive
		? FVector2D::ZeroVector
		: MoveInput;
	if (bRemoteStopVisualIntentActive)
	{
		bHasMoveInput = false;
		MoveInputSize = 0.0f;
		MoveInputHeldTime = 0.0f;
		MoveInputTurnAngle = 0.0f;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, VisualMoveInput, false);
}

bool UProject_JLocomotionAnimStateComponent::ConsumeRemoteStopStartSuppress(float DeltaTime)
{
	const bool bSuppressStartFromResidualVelocity = RemoteStopStartSuppressTimeRemaining > 0.0f;
	RemoteStopStartSuppressTimeRemaining = FMath::Max(0.0f, RemoteStopStartSuppressTimeRemaining - DeltaTime);
	return bSuppressStartFromResidualVelocity;
}

void UProject_JLocomotionAnimStateComponent::ApplyRemoteStopStartSuppress()
{
	bPendingStartRequest = false;
	ClearResolvedMoveInputState();
}

void UProject_JLocomotionAnimStateComponent::RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput, bool bTrackTurnAngle)
{
	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	bSharpTurnRequested = false;

	if (bTrackTurnAngle && bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingFromMovementInput(const FVector2D& MoveInput, bool bAllowSprintTurnCancel)
{
	if (TryFinishLandingFromInputChange())
	{
		return true;
	}

	if (TryFinishLandingRedirectCancel(MoveInput))
	{
		return true;
	}

	return bAllowSprintTurnCancel && TryFinishSprintLandingTurnCancel(MoveInput);
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingFromInputChange()
{
	if (!IsLandingStateActive())
	{
		return false;
	}

	if (ShouldUseLocalInputState() && LandingElapsedTime < LandingInputCancelGraceTime)
	{
		return false;
	}

	if (!bLandWasMoving && bHasMoveInput)
	{
		bLandWasMoving = true;
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	if (bLandWasMoving && !bHasMoveInput)
	{
		bLandWasMoving = false;
		bLandWasSprinting = false;
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingRedirectCancel(const FVector2D& MoveInput)
{
	if (!IsLandingStateActive() ||
		!bLandWasMoving ||
		!bHasMoveInput ||
		LandingElapsedTime < LandingRedirectCancelMinTime)
	{
		return false;
	}

	if (HasLandingDirectionTurnCancel(MoveInput, LandingRedirectCancelAngle) ||
		HasLandingActorTurnCancel(LandingRedirectCancelAngle))
	{
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryFinishSprintLandingTurnCancel(const FVector2D& MoveInput)
{
	if (!IsLandingStateActive() ||
		!bLandWasSprinting ||
		!bWantsSprint ||
		!bHasMoveInput ||
		LandingElapsedTime < SprintLandingTurnCancelMinTime)
	{
		return false;
	}

	if (HasLandingDirectionTurnCancel(MoveInput, SprintLandingTurnCancelAngle) ||
		HasLandingActorTurnCancel(SprintLandingTurnCancelAngle))
	{
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::HasLandingDirectionTurnCancel(const FVector2D& MoveInput, float AngleThreshold)
{
	const FVector CurrentLandingMoveWorldDirection = CalculateMoveWorldDirection(MoveInput);
	const FVector ReferenceLandingMoveWorldDirection = !InitialLandingMoveWorldDirection.IsNearlyZero()
		? InitialLandingMoveWorldDirection
		: PreviousLandingMoveWorldDirection;

	if (!ReferenceLandingMoveWorldDirection.IsNearlyZero() && !CurrentLandingMoveWorldDirection.IsNearlyZero())
	{
		const float DirectionDot = FMath::Clamp(FVector::DotProduct(ReferenceLandingMoveWorldDirection, CurrentLandingMoveWorldDirection), -1.0f, 1.0f);
		const float DirectionAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
		if (DirectionAngle >= AngleThreshold)
		{
			return true;
		}
	}

	PreviousLandingMoveWorldDirection = CurrentLandingMoveWorldDirection;
	return false;
}

bool UProject_JLocomotionAnimStateComponent::HasLandingActorTurnCancel(float AngleThreshold)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return false;
	}

	const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
	const float InitialYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(InitialLandingActorYaw, CurrentActorYaw));
	const float PreviousYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousLandingActorYaw, CurrentActorYaw));
	if (FMath::Max(InitialYawDelta, PreviousYawDelta) >= AngleThreshold)
	{
		return true;
	}

	PreviousLandingActorYaw = CurrentActorYaw;
	return false;
}
