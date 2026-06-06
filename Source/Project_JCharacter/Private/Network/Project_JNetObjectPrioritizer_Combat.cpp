#include "Network/Project_JNetObjectPrioritizer_Combat.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "Project_JGameplayTags.h"

float UProject_JNetObjectPrioritizer_Combat::CalculatePriorityMultiplier(const AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return 0.0f;
	}

	UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(TargetActor));
	if (!AbilitySystemComponent)
	{
		return 1.0f;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Dead))
	{
		return DeadPriorityMultiplier;
	}

	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Attacking) ||
		AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_Dodging) ||
		AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_HitReacting))
	{
		return ActiveCombatPriorityMultiplier;
	}

	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTags.State_CombatMode))
	{
		return CombatModePriorityMultiplier;
	}

	return 1.0f;
}

bool UProject_JNetObjectPrioritizer_Combat::HasCombatPriority(const AActor* TargetActor) const
{
	return CalculatePriorityMultiplier(TargetActor) > 1.0f;
}

FProject_JReplicationPolicyDecision UProject_JNetObjectPrioritizer_Combat::ApplyCombatPriority(const AActor* TargetActor, FProject_JReplicationPolicyDecision Decision) const
{
	const float PriorityMultiplier = CalculatePriorityMultiplier(TargetActor);
	Decision.PriorityMultiplier = FMath::Max(Decision.PriorityMultiplier, PriorityMultiplier);
	if (PriorityMultiplier > 1.0f)
	{
		Decision.AddReason(EProject_JReplicationRelevanceReason::Combat);
	}
	return Decision;
}
