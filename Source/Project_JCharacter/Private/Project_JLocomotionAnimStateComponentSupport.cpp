// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

namespace
{
const TCHAR* ToDebugString(EProject_JGroundMotionMode MotionMode)
{
	switch (MotionMode)
	{
	case EProject_JGroundMotionMode::Idle:
		return TEXT("Idle");
	case EProject_JGroundMotionMode::Start:
		return TEXT("Start");
	case EProject_JGroundMotionMode::Locomotion:
		return TEXT("Locomotion");
	case EProject_JGroundMotionMode::Stop:
		return TEXT("Stop");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionGaitIntent GaitIntent)
{
	switch (GaitIntent)
	{
	case EProject_JLocomotionGaitIntent::Walk:
		return TEXT("Walk");
	case EProject_JLocomotionGaitIntent::Run:
		return TEXT("Run");
	case EProject_JLocomotionGaitIntent::Sprint:
		return TEXT("Sprint");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionRotationMode RotationMode)
{
	switch (RotationMode)
	{
	case EProject_JLocomotionRotationMode::OrientToMovement:
		return TEXT("Orient");
	case EProject_JLocomotionRotationMode::Strafe:
		return TEXT("Strafe");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(EProject_JLocomotionPhaseFamily PhaseFamily)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Idle:
		return TEXT("Idle");
	case EProject_JLocomotionPhaseFamily::Start:
		return TEXT("Start");
	case EProject_JLocomotionPhaseFamily::Cycle:
		return TEXT("Cycle");
	case EProject_JLocomotionPhaseFamily::Stop:
		return TEXT("Stop");
	case EProject_JLocomotionPhaseFamily::Pivot:
		return TEXT("Pivot");
	case EProject_JLocomotionPhaseFamily::Turn:
		return TEXT("Turn");
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		return TEXT("TurnInPlace");
	case EProject_JLocomotionPhaseFamily::JumpStart:
		return TEXT("JumpStart");
	case EProject_JLocomotionPhaseFamily::Fall:
		return TEXT("Fall");
	case EProject_JLocomotionPhaseFamily::Landing:
		return TEXT("Landing");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* ToDebugString(ENetRole NetRole)
{
	switch (NetRole)
	{
	case ROLE_None:
		return TEXT("None");
	case ROLE_SimulatedProxy:
		return TEXT("SimProxy");
	case ROLE_AutonomousProxy:
		return TEXT("AutoProxy");
	case ROLE_Authority:
		return TEXT("Authority");
	default:
		return TEXT("Unknown");
	}
}
}

FString UProject_JLocomotionAnimStateComponent::GetDebugSummary() const
{
	const AActor* Owner = GetOwner();
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const ENetMode NetMode = Owner ? Owner->GetNetMode() : NM_Standalone;
	const ENetRole LocalRole = Owner ? Owner->GetLocalRole() : ROLE_None;
	const ENetRole RemoteRole = Owner ? Owner->GetRemoteRole() : ROLE_None;
	const bool bSprintAllowed = PlayerOwner && PlayerOwner->IsSprintLocomotionAllowed();
	const bool bJumpAllowed = PlayerOwner && PlayerOwner->IsJumpLocomotionAllowed();
	const bool bCombatMode = PlayerOwner && PlayerOwner->IsCombatModeActive();
	const bool bAttacking = PlayerOwner && PlayerOwner->IsAttacking();
	const bool bDodging = PlayerOwner && PlayerOwner->IsDodging();
	const bool bHitReacting = PlayerOwner && PlayerOwner->IsHitReacting();

	return FString::Printf(
		TEXT("Net=%s LocalRole=%s RemoteRole=%s LocalInput=%s Rendered=%s Dedicated=%s\n")
		TEXT("Ground=%s GroundSpeed=%.1f VerticalSpeed=%.1f HasInput=%s InputSize=%.2f Held=%.2f Turn=%.1f SharpTurn=%s\n")
		TEXT("Context Gait=%s Rotation=%s Phase=%s Moving=%s Starting=%s Pivoting=%s TurnInPlace=%s Spin=%s DesiredYaw=%.1f Accel=%.2f\n")
		TEXT("Policy SprintAllowed=%s JumpAllowed=%s Combat=%s Attack=%s Dodge=%s HitReact=%s\n")
		TEXT("Sprint Wants=%s UseSprint=%s StartSprint=%s StopSprint=%s StartReq=%s StopReq=%s\n")
		TEXT("Air InAir=%s PhysAir=%s Jumping=%s FallOff=%s Landing=%s LandReq=%s CanLand=%s CanGround=%s LastFall=%.1f\n")
		TEXT("JumpDebug IgnoredLandings=%d LastIgnoredElapsed=%.3f LastIgnoredVz=%.1f LastIgnoredFall=%.1f LastIgnoredVertical=%.1f HadFallEvidence=%s\n")
		TEXT("Combat Dir=%.1f Fwd=%.2f Right=%.2f FwdSpeed=%.1f RightSpeed=%.1f"),
		ToDebugString(NetMode),
		ToDebugString(LocalRole),
		ToDebugString(RemoteRole),
		bUsingLocalInputState ? TEXT("true") : TEXT("false"),
		bRecentlyRendered ? TEXT("true") : TEXT("false"),
		bDedicatedServerContext ? TEXT("true") : TEXT("false"),
		ToDebugString(GroundMotionMode),
		GroundSpeed,
		VerticalSpeed,
		bHasMoveInput ? TEXT("true") : TEXT("false"),
		MoveInputSize,
		MoveInputHeldTime,
		MoveInputTurnAngle,
		bSharpTurnRequested ? TEXT("true") : TEXT("false"),
		ToDebugString(AuthoritativeContext.GaitIntent),
		ToDebugString(AuthoritativeContext.RotationMode),
		ToDebugString(DerivedLocomotionContext.PhaseFamily),
		DerivedLocomotionContext.bIsMoving ? TEXT("true") : TEXT("false"),
		DerivedLocomotionContext.bIsStarting ? TEXT("true") : TEXT("false"),
		DerivedLocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
		DerivedLocomotionContext.bShouldTurnInPlace ? TEXT("true") : TEXT("false"),
		DerivedLocomotionContext.bShouldSpinTransition ? TEXT("true") : TEXT("false"),
		KinematicContext.DesiredFacingDeltaYaw,
		KinematicContext.AccelerationRatio,
		bSprintAllowed ? TEXT("true") : TEXT("false"),
		bJumpAllowed ? TEXT("true") : TEXT("false"),
		bCombatMode ? TEXT("true") : TEXT("false"),
		bAttacking ? TEXT("true") : TEXT("false"),
		bDodging ? TEXT("true") : TEXT("false"),
		bHitReacting ? TEXT("true") : TEXT("false"),
		bWantsSprint ? TEXT("true") : TEXT("false"),
		bUseSprintLocomotion ? TEXT("true") : TEXT("false"),
		bStartWasSprinting ? TEXT("true") : TEXT("false"),
		bStopWasSprinting ? TEXT("true") : TEXT("false"),
		bStartRequested ? TEXT("true") : TEXT("false"),
		bStopRequested ? TEXT("true") : TEXT("false"),
		bIsInAir ? TEXT("true") : TEXT("false"),
		bIsPhysicallyInAir ? TEXT("true") : TEXT("false"),
		bIsJumping ? TEXT("true") : TEXT("false"),
		bIsFallOffStart ? TEXT("true") : TEXT("false"),
		bIsLanding ? TEXT("true") : TEXT("false"),
		bLandingRequested ? TEXT("true") : TEXT("false"),
		bCanEnterLand ? TEXT("true") : TEXT("false"),
		bCanEnterGround ? TEXT("true") : TEXT("false"),
		LastFallSpeed,
		IgnoredJumpStartLandingCount,
		LastIgnoredJumpStartLandingElapsedTime,
		LastIgnoredJumpStartLandingVelocityZ,
		LastIgnoredJumpStartLandingFallSpeed,
		LastIgnoredJumpStartLandingVerticalSpeed,
		bLastIgnoredJumpStartLandingHadFallingEvidence ? TEXT("true") : TEXT("false"),
		MovementDirection,
		CombatInputForward,
		CombatInputRight,
		CombatForwardSpeed,
		CombatRightSpeed);
}

void UProject_JLocomotionAnimStateComponent::ResetJumpStartLandingDebugState()
{
	IgnoredJumpStartLandingCount = 0;
	LastIgnoredJumpStartLandingElapsedTime = 0.0f;
	LastIgnoredJumpStartLandingVelocityZ = 0.0f;
	LastIgnoredJumpStartLandingFallSpeed = 0.0f;
	LastIgnoredJumpStartLandingVerticalSpeed = 0.0f;
	bLastIgnoredJumpStartLandingHadFallingEvidence = false;
}

void UProject_JLocomotionAnimStateComponent::UpdateCombatMovementState(const FVector& HorizontalVelocity)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		ClearCombatMovementState();
		return;
	}

	if (!PlayerOwner->IsCombatModeActive() || !bHasMoveInput)
	{
		ClearCombatMovementState();
		return;
	}

	if (bUsingLocalInputState)
	{
		UpdateLocalCombatMovementState(*PlayerOwner);
		return;
	}

	UpdateRemoteCombatMovementState(*PlayerOwner, HorizontalVelocity);
}

void UProject_JLocomotionAnimStateComponent::ClearCombatMovementState()
{
	MovementDirection = 0.0f;
	CombatInputForward = 0.0f;
	CombatInputRight = 0.0f;
	CombatForwardSpeed = 0.0f;
	CombatRightSpeed = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::UpdateLocalCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner)
{
	const FVector2D CombatMoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const float DesiredSpeed = MovementComponent ? MovementComponent->MaxWalkSpeed : PlayerOwner.WalkSpeed;
	CombatInputRight = CombatMoveInput.X;
	CombatInputForward = CombatMoveInput.Y;
	CombatRightSpeed = CombatMoveInput.X * DesiredSpeed;
	CombatForwardSpeed = CombatMoveInput.Y * DesiredSpeed;
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatMoveInput.X, CombatMoveInput.Y));
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner, const FVector& HorizontalVelocity)
{
	const FVector Forward = PlayerOwner.GetActorForwardVector();
	const FVector Right = PlayerOwner.GetActorRightVector();
	CombatForwardSpeed = FVector::DotProduct(HorizontalVelocity, Forward);
	CombatRightSpeed = FVector::DotProduct(HorizontalVelocity, Right);

	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const float MaxSpeed = MovementComponent ? FMath::Max(MovementComponent->MaxWalkSpeed, 1.0f) : FMath::Max(PlayerOwner.WalkSpeed, 1.0f);
	CombatInputForward = FMath::Clamp(CombatForwardSpeed / MaxSpeed, -1.0f, 1.0f);
	CombatInputRight = FMath::Clamp(CombatRightSpeed / MaxSpeed, -1.0f, 1.0f);
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatRightSpeed, CombatForwardSpeed));
}

void UProject_JLocomotionAnimStateComponent::DispatchLandingCancelForAnimation()
{
	if (!ShouldUseLocalInputState() || bLandingCancelEventDispatched)
	{
		return;
	}

	if (AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		bLandingCancelEventDispatched = true;
		PlayerOwner->NotifyLandingCancelledForAnimation();
	}
}

void UProject_JLocomotionAnimStateComponent::AddOwnedInAirGameplayTag()
{
	if (bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		bAppliedInAirGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedInAirGameplayTag()
{
	if (!bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
	}
	bAppliedInAirGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::AddOwnedLandingGameplayTag()
{
	if (bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		bAppliedLandingGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedLandingGameplayTag()
{
	if (!bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
	}
	bAppliedLandingGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::ClearOwnedMovementGameplayTags()
{
	RemoveOwnedInAirGameplayTag();
	RemoveOwnedLandingGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::ClearMovementRequests()
{
	bStartRequested = false;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
	ResetGroundMotionTransitionRequests();
	bResolvedMoveInputLastUpdate = false;
	MoveInputHeldTime = 0.0f;
	StopElapsedTime = 0.0f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;
	EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
}

void UProject_JLocomotionAnimStateComponent::ClearTransientAnimationRequests()
{
	bSharpTurnRequested = false;
	bStartRequested = false;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
	ClearPendingAnimationExitRequests();
	bLandingCancelEventDispatched = false;
	bRealLandingEventRequested = false;
	bResolvedMoveInputLastUpdate = bHasMoveInput;
	EnterGroundMotionMode(bHasMoveInput ? EProject_JGroundMotionMode::Locomotion : EProject_JGroundMotionMode::Idle);
	ClearLandingTimers();
}

void UProject_JLocomotionAnimStateComponent::ClearPendingAnimationExitRequests()
{
	bLandingFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::ClearResolvedMoveInputState()
{
	bHasMoveInput = false;
	bPrevHasMoveInput = false;
	bResolvedMoveInputLastUpdate = false;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;
}
