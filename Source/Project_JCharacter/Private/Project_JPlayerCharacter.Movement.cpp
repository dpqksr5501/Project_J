// Movement policy stays on the game thread; frame-varying facing/input is not cached.

#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/AnimSequence.h"
#include "Combat/Project_JCombatMovementPolicy.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Movement/Project_JGroundMovement.h"
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
	// Physical facing is part of the predicted Mover command. GASP handles pose/root offset.
	bUseControllerRotationYaw = false;
	SetStrafe(bEnableCombatRotation && ShouldUseCombatRotationMode());
}



bool AProject_JPlayerCharacter::IsCombatActionBlockingSprint() const
{
	return BuildCombatMovementPolicy(*this).IsSprintBlocked();
}

void AProject_JPlayerCharacter::UpdateMaxWalkSpeed()
{
	SetSprint(IsSprintLocomotionAllowed());
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

 return UProject_JGroundMovementLibrary::SupportsStrafeSprint(
  FVector(SprintMoveInput.Y,SprintMoveInput.X,0.),FVector::ForwardVector);
}

bool AProject_JPlayerCharacter::IsJumpLocomotionAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsJumpAllowed();
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
