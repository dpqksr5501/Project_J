#include "Mount/Project_JMountTypes.h"

#include "Project_JGameplayTags.h"

EProject_JMountEligibilityFailure Project_J::Mount::EvaluateRiderGameplayTags(
	const FGameplayTagContainer& RiderTags)
{
	const FProject_JGameplayTags& Tags = FProject_JGameplayTags::Get();
	if (RiderTags.HasTag(Tags.State_Dead))
	{
		return EProject_JMountEligibilityFailure::RiderDead;
	}
	if (RiderTags.HasTag(Tags.State_CombatMode) || RiderTags.HasTag(Tags.State_CombatTransition))
	{
		return EProject_JMountEligibilityFailure::RiderInCombat;
	}
	if (RiderTags.HasTag(Tags.State_Attacking) ||
		RiderTags.HasTag(Tags.State_Dodging) ||
		RiderTags.HasTag(Tags.State_HitReacting))
	{
		return EProject_JMountEligibilityFailure::RiderBusy;
	}

	return EProject_JMountEligibilityFailure::None;
}
