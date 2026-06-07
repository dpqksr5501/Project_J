#include "Combat/Project_JCombatMovementPolicy.h"

bool FProject_JCombatMovementPolicy::IsSprintBlocked() const
{
	return
		(bCombatMode && !bAllowSprintInCombat) ||
		bAttacking ||
		bDodging ||
		bHitReacting;
}

bool FProject_JCombatMovementPolicy::IsJumpAllowed() const
{
	return !bAttacking && !bDodging && !bHitReacting;
}

bool FProject_JCombatMovementPolicy::IsGroundStartAllowed() const
{
	return !bHitReacting;
}

bool FProject_JCombatMovementPolicy::IsGroundStopAllowed() const
{
	return true;
}

bool FProject_JCombatMovementPolicy::IsCombatLocomotionOverlayAllowed() const
{
	return bCombatMode && !bHitReacting;
}

bool FProject_JCombatMovementPolicy::ShouldUseCombatRotationMode() const
{
	return bUseCombatRotationMode;
}

bool FProject_JCombatMovementPolicy::ShouldInterruptIntroOnHit() const
{
	return bInterruptIntroOnHit;
}
