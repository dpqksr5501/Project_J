// Movement policy stays on the game thread; frame-varying facing/input is not cached.

#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/AnimSequence.h"
#include "Combat/Project_JCombatMovementPolicy.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Project_JLocomotionAnimStateComponent.h"

namespace
{
FProject_JCombatMovementPolicy BuildCombatMovementPolicy(const AProject_JPlayerCharacter& PlayerCharacter)
{
	FProject_JCombatMovementPolicy Policy;
	Policy.bCombatMode = PlayerCharacter.IsCombatModeActive();
	Policy.bAttacking = PlayerCharacter.IsAttacking();
	Policy.bDodging = PlayerCharacter.IsDodging();
	Policy.bHitReacting = PlayerCharacter.IsHitReacting();
	if (const UProject_JCombatAnimProfile* CombatAnimProfile = PlayerCharacter.GetCombatAnimProfile())
	{
		Policy.bAllowSprintInCombat = CombatAnimProfile->bAllowSprintInCombat;
		Policy.bUseCombatRotationMode = CombatAnimProfile->bUseCombatRotationMode;
		Policy.bInterruptIntroOnHit = CombatAnimProfile->bInterruptCombatIntroOnHit;
	}
	return Policy;
}
}

void AProject_JPlayerCharacter::ApplyCombatRotationMode(bool bEnableCombatRotation)
{
	const bool bIsInAir = GetCharacterMovement() && GetCharacterMovement()->IsFalling();
	const bool bShouldUseCombatRotation = bEnableCombatRotation && ShouldUseCombatRotationMode();
	const bool bIsMovingInCombat = bShouldUseCombatRotation &&
		(GetPendingMovementInputVector().SizeSquared() > 0.001f || GetVelocity().SizeSquared2D() > 100.0f);

	bool bRotationModeChanged = false;
	if (bShouldUseCombatRotation && bIsInAir)
	{
		bRotationModeChanged = bUseControllerRotationYaw != false;
		bUseControllerRotationYaw = false;

		const float TargetYaw = GetController() ? GetController()->GetControlRotation().Yaw : GetActorRotation().Yaw;
		const FRotator CurrentRot = GetActorRotation();
		const FRotator TargetRot(0.0f, TargetYaw, 0.0f);
		const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
		const float CatchUpSpeed = GetLocomotionProfile()
			? GetLocomotionProfile()->MotionMatchingSearchPolicy.AirRotationCatchUpSpeed
			: 12.0f;
		const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, CatchUpSpeed);
		SetActorRotation(NewRot);
	}
	else
	{
		const bool bDesiredUseControllerRotationYaw = bIsMovingInCombat;
		bRotationModeChanged = bUseControllerRotationYaw != bDesiredUseControllerRotationYaw;
		bUseControllerRotationYaw = bDesiredUseControllerRotationYaw;
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = !bShouldUseCombatRotation;
	}

	bool bCurrentlyInTurnInPlace = false;
	bool bApplyingLocalTurnInPlaceRootYaw = false;
	const int32 TipTraceMode = Project_J::MotionMatchingCVars::GetTurnInPlaceTraceMode();
	if (bShouldUseCombatRotation && !bIsMovingInCombat)
	{
		if (const UProject_JCharacterAnimInstance* AnimInst = Cast<UProject_JCharacterAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
		{
			const EProject_JStateControllerPresentationState PresentationState =
				AnimInst->GetThreadSafeStateControllerPresentationState();
			bCurrentlyInTurnInPlace = (PresentationState == EProject_JStateControllerPresentationState::TurnInPlace);
			const UAnimationAsset* SelectedAnim = AnimInst->GetThreadSafeStateControllerSelectedAnimation();
			if (bCurrentlyInTurnInPlace)
			{
				if (const UAnimSequence* AnimSeq = Cast<UAnimSequence>(SelectedAnim))
				{
					const int32 SelectionRevision = AnimInst->GetThreadSafeStateControllerSelectionRevision();
					const bool bNewSelection = TurnInPlacePresentationRuntime.BeginSelection(
						AnimSeq, SelectionRevision, GetActorRotation().Yaw);

					const float Elapsed = AnimInst->GetThreadSafeStateControllerPlaybackHoldElapsedTime();
					const float CurrTime = FMath::Clamp(Elapsed, 0.0f, AnimSeq->GetPlayLength());
					const float StartTime = FMath::Clamp(
						AnimInst->GetThreadSafeStateControllerSelectedAnimationStartTime(),
						0.0f,
						AnimSeq->GetPlayLength());
					const float CumulativeCurrentTime = FMath::Clamp(StartTime + CurrTime, StartTime, AnimSeq->GetPlayLength());

					FAnimExtractContext CurrentCumulativeContext(static_cast<double>(CumulativeCurrentTime));
					const float CurrentCumulativeYaw = AnimSeq->ExtractRootMotionFromRange(
						static_cast<double>(StartTime), static_cast<double>(CumulativeCurrentTime), CurrentCumulativeContext).Rotator().Yaw;

					const float AuthoredTargetActorYaw = FRotator::NormalizeAxis(
						TurnInPlacePresentationRuntime.GetSelectionStartActorYaw() + CurrentCumulativeYaw);
					const float RootYawDelta = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, AuthoredTargetActorYaw);

					// TIP owns one fixed authored target. Prefer the locomotion context so
					// the capsule, Blend Stack Steering and replicated event agree.
					float FacingDelta = RootYawDelta;
					if (LocomotionAnimStateComponent)
					{
						FacingDelta = LocomotionAnimStateComponent->KinematicContext.DesiredFacingDeltaYaw;
					}
					else if (IsLocallyControlled() && GetController())
					{
						FacingDelta = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, GetController()->GetControlRotation().Yaw);
					}

					const float ClampedRootYawDelta = FProject_JTurnInPlacePresentationRuntime::ClampAuthoredYaw(
						RootYawDelta, FacingDelta);

					// A reversed TIP spends a short release window unwinding Blend Stack /
					// Offset Root Bone. Do not continue applying the old authored root yaw
					// to the capsule during that visual hand-off.
					const bool bCanApplyActorRotation = IsLocallyControlled() &&
						(!LocomotionAnimStateComponent || LocomotionAnimStateComponent->IsLocalTurnInPlaceTargetActive());
					const float ActorYawBefore = GetActorRotation().Yaw;
					if (bCanApplyActorRotation && !FMath::IsNearlyZero(ClampedRootYawDelta))
					{
						AddActorWorldRotation(FRotator(0.0f, ClampedRootYawDelta, 0.0f));
					}
					bApplyingLocalTurnInPlaceRootYaw = bCanApplyActorRotation;
					const bool bFacingOpposesAuthoredTurn =
						FMath::Abs(FacingDelta) > 1.0f && FMath::Abs(RootYawDelta) > 1.0f &&
						FMath::Sign(FacingDelta) != FMath::Sign(RootYawDelta);
					if (TipTraceMode >= 2 ||
						(TipTraceMode == 1 && (bNewSelection || CurrTime <= 0.35f ||
							bFacingOpposesAuthoredTurn || FMath::Abs(ClampedRootYawDelta) >= 6.0f)))
					{
						const UWorld* TraceWorld = GetWorld();
						const int32 SemanticSequence = LocomotionAnimStateComponent
							? LocomotionAnimStateComponent->DerivedLocomotionContext.TurnInPlaceSequence : 0;
						const uint8 SemanticBucket = LocomotionAnimStateComponent
							? LocomotionAnimStateComponent->DerivedLocomotionContext.TurnInPlaceDirectionBucket : 0;
						UE_LOG(LogProjectJPlayer, Display,
							TEXT("TIPTrace Stage=Root T=%.3f Actor=%s Local=%d NewSelection=%d Seq=%d Bucket=%d Rev=%d Asset=%s Hold=%.3f Start=%.3f Playhead=%.3f RootCum=%.2f Anchor=%.2f ActorBefore=%.2f ActorAfter=%.2f AuthoredDelta=%.2f FacingDelta=%.2f Applied=%.2f TargetActive=%d TargetYaw=%.2f Opposite=%d"),
							TraceWorld ? TraceWorld->GetTimeSeconds() : 0.0f, *GetName(),
							IsLocallyControlled() ? 1 : 0, bNewSelection ? 1 : 0,
							SemanticSequence, static_cast<int32>(SemanticBucket), SelectionRevision,
							*GetNameSafe(AnimSeq), Elapsed, StartTime, CumulativeCurrentTime,
							CurrentCumulativeYaw, TurnInPlacePresentationRuntime.GetSelectionStartActorYaw(),
							ActorYawBefore, GetActorRotation().Yaw, RootYawDelta, FacingDelta,
							bCanApplyActorRotation ? ClampedRootYawDelta : 0.0f,
							LocomotionAnimStateComponent && LocomotionAnimStateComponent->IsLocalTurnInPlaceTargetActive() ? 1 : 0,
							LocomotionAnimStateComponent ? LocomotionAnimStateComponent->KinematicContext.DesiredFacingYaw : 0.0f,
							bFacingOpposesAuthoredTurn ? 1 : 0);
					}

					if (!HasAuthority() && bCanApplyActorRotation)
					{
						const UWorld* World = GetWorld();
						const double Now = World ? World->GetTimeSeconds() : 0.0;
						const float CurrentActorYaw = GetActorRotation().Yaw;
						if (TurnInPlacePresentationRuntime.ShouldSendActiveYaw(CurrentActorYaw, Now))
						{
							ServerSetTurnInPlaceRotation(true, CurrentActorYaw);
						}
					}
				}
				else if (TipTraceMode > 0)
				{
					UE_LOG(LogProjectJPlayer, Display,
						TEXT("TIPTrace Stage=Root Event=NoSequence Actor=%s Rev=%d Asset=%s"),
						*GetName(), AnimInst->GetThreadSafeStateControllerSelectionRevision(), *GetNameSafe(SelectedAnim));
				}
			}
		}
	}

	if (TurnInPlacePresentationRuntime.FinishFrame(
		bCurrentlyInTurnInPlace, bApplyingLocalTurnInPlaceRootYaw, GetActorRotation().Yaw,
		!HasAuthority() && IsLocallyControlled()))
	{
		ServerSetTurnInPlaceRotation(false, GetActorRotation().Yaw);
	}

	if (bRotationModeChanged && MotionMatchingTrajectoryComponent)
	{
		MotionMatchingTrajectoryComponent->ResetTrajectoryHistoryWithReason(
			EProject_JTrajectoryResetReason::RotationModeChanged);
	}
}

bool AProject_JPlayerCharacter::AllowsStraightRunningTrajectoryRepair() const
{
	// Combat currently owns yaw through the controller and therefore permits
	// movement that intentionally differs from facing. Future lock-on or forced
	// facing modes should return false here as well, rather than changing the
	// global simulated-proxy repair CVar.
	return !bUseControllerRotationYaw;
}

bool AProject_JPlayerCharacter::IsCombatActionBlockingSprint() const
{
	return BuildCombatMovementPolicy(*this).IsSprintBlocked();
}

void AProject_JPlayerCharacter::UpdateMaxWalkSpeed()
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp || (!IsLocallyControlled() && GetLocalRole() != ROLE_Authority))
	{
		return;
	}

	const bool bCanSprint = IsSprintLocomotionAllowed();
	const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile();
	const float EffectiveWalkSpeed = EffectiveLocomotionProfile ? EffectiveLocomotionProfile->WalkSpeed : WalkSpeed;
	const float EffectiveSprintSpeed = EffectiveLocomotionProfile ? EffectiveLocomotionProfile->SprintSpeed : SprintSpeed;
	const float EffectiveWalkRotationRateYaw = EffectiveLocomotionProfile
		? EffectiveLocomotionProfile->WalkRotationRateYaw
		: WalkRotationRateYaw;
	const float EffectiveSprintRotationRateYaw = EffectiveLocomotionProfile
		? EffectiveLocomotionProfile->SprintRotationRateYaw
		: SprintRotationRateYaw;
	float DirectionalSpeedMultiplier = 1.0f;
	if (EffectiveLocomotionProfile)
	{
		const FProject_JLocomotionMovementPolicy& Policy = EffectiveLocomotionProfile->MovementPolicy;
		const float DesiredMaxAcceleration = bCanSprint ? Policy.SprintMaxAcceleration : Policy.RunMaxAcceleration;
		const float DesiredBrakingDeceleration = bCanSprint ? Policy.SprintBrakingDeceleration : Policy.RunBrakingDeceleration;
		const float DesiredGroundFriction = bCanSprint ? Policy.SprintGroundFriction : Policy.RunGroundFriction;
		if (!FMath::IsNearlyEqual(MoveComp->MaxAcceleration, DesiredMaxAcceleration))
		{
			MoveComp->MaxAcceleration = DesiredMaxAcceleration;
		}
		if (!FMath::IsNearlyEqual(MoveComp->BrakingDecelerationWalking, DesiredBrakingDeceleration))
		{
			MoveComp->BrakingDecelerationWalking = DesiredBrakingDeceleration;
		}
		if (!FMath::IsNearlyEqual(MoveComp->GroundFriction, DesiredGroundFriction))
		{
			MoveComp->GroundFriction = DesiredGroundFriction;
		}

		if (Policy.bEnableStrafeDirectionalSpeedScaling && IsCombatModeActive())
		{
			FVector Direction = GetPendingMovementInputVector();
			Direction.Z = 0.0f;
			if (Direction.IsNearlyZero())
			{
				Direction = GetVelocity();
				Direction.Z = 0.0f;
			}

			if (!Direction.IsNearlyZero())
			{
				const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(Direction.GetSafeNormal());
				const float ForwardAmount = LocalDirection.X;
				const float SideAmount = FMath::Abs(LocalDirection.Y);
				if (ForwardAmount < -0.5f)
				{
					DirectionalSpeedMultiplier = Policy.StrafeBackwardSpeedMultiplier;
				}
				else if (SideAmount > FMath::Abs(ForwardAmount))
				{
					DirectionalSpeedMultiplier = Policy.StrafeSideSpeedMultiplier;
				}
				else
				{
					DirectionalSpeedMultiplier = Policy.StrafeForwardSpeedMultiplier;
				}
			}
		}
	}

	const float DesiredMaxWalkSpeed =
		(bCanSprint ? EffectiveSprintSpeed : EffectiveWalkSpeed) * DirectionalSpeedMultiplier;
	const float DesiredRotationRateYaw = bCanSprint
		? EffectiveSprintRotationRateYaw
		: EffectiveWalkRotationRateYaw;
	if (!FMath::IsNearlyEqual(MoveComp->MaxWalkSpeed, DesiredMaxWalkSpeed))
	{
		MoveComp->MaxWalkSpeed = DesiredMaxWalkSpeed;
	}
	if (!FMath::IsNearlyEqual(MoveComp->RotationRate.Yaw, DesiredRotationRateYaw))
	{
		MoveComp->RotationRate = FRotator(0.0f, DesiredRotationRateYaw, 0.0f);
	}
}

bool AProject_JPlayerCharacter::ShouldUseCombatRotationMode() const
{
	return BuildCombatMovementPolicy(*this).ShouldUseCombatRotationMode();
}

bool AProject_JPlayerCharacter::ShouldInterruptCombatIntroOnHit() const
{
	FProject_JCombatMovementPolicy Policy = BuildCombatMovementPolicy(*this);
	if (!GetCombatAnimProfile())
	{
		Policy.bInterruptIntroOnHit = bInterruptCombatIntroOnHit;
	}
	return Policy.ShouldInterruptIntroOnHit();
}

bool AProject_JPlayerCharacter::IsSprintLocomotionAllowed() const
{
	return CombatStateComponent && CombatStateComponent->IsSprintTagActive() && !IsCombatActionBlockingSprint();
}

bool AProject_JPlayerCharacter::IsSprintInputDirectionAllowed() const
{
	// Simulated proxies have no raw input. Their replicated Sprint tag remains authoritative.
	if (!IsLocallyControlled() || !IsCombatModeActive())
	{
		return true;
	}

	const UProject_JCombatAnimProfile* CombatAnimProfile = GetCombatAnimProfile();
	if (!CombatAnimProfile || !CombatAnimProfile->bAllowSprintInCombat)
	{
		return false;
	}

	return !CombatAnimProfile->bRequireForwardInputForSprintInCombat ||
		SprintMoveInput.Y > CombatAnimProfile->CombatSprintForwardInputThreshold;
}

bool AProject_JPlayerCharacter::IsJumpLocomotionAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsJumpAllowed();
}

bool AProject_JPlayerCharacter::IsGroundStartAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsGroundStartAllowed();
}

bool AProject_JPlayerCharacter::IsGroundStopAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsGroundStopAllowed();
}

bool AProject_JPlayerCharacter::IsCombatLocomotionOverlayAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsCombatLocomotionOverlayAllowed();
}

void AProject_JPlayerCharacter::StartSprint()
{
	bSprintInputHeld = true;
	RefreshSprintAbilityFromInput();
}

void AProject_JPlayerCharacter::StopSprint()
{
	bSprintInputHeld = false;
	CancelAbilitiesByTag(SprintAbilityTag);
}

void AProject_JPlayerCharacter::UpdateSprintInputFromMove(const FVector2D& MoveInput)
{
	SprintMoveInput = MoveInput.GetClampedToMaxSize(1.0f);
	RefreshSprintAbilityFromInput();
}

bool AProject_JPlayerCharacter::ShouldRequestSprintAbility() const
{
	return bSprintInputHeld && IsSprintInputDirectionAllowed();
}

void AProject_JPlayerCharacter::RefreshSprintAbilityFromInput()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const bool bSprintAbilityActive = CombatStateComponent && CombatStateComponent->IsSprintTagActive();
	if (ShouldRequestSprintAbility())
	{
		if (!bSprintAbilityActive)
		{
			TryActivateAbilityByTag(SprintAbilityTag);
		}
	}
	else if (bSprintAbilityActive)
	{
		CancelAbilitiesByTag(SprintAbilityTag);
	}
}
