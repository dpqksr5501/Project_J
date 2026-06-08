// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Project_JAbilitySystemComponent.h"

UProject_JCombatComponent::UProject_JCombatComponent()
{
	// Disable ticking by default as it's not needed for base combat capabilities
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		BindToGAS(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner));
	}
}

void UProject_JCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearOwnedCombatStateTags();

	if (OwnerASC && AbilityActivatedDelegateHandle.IsValid())
	{
		OwnerASC->AbilityActivatedCallbacks.Remove(AbilityActivatedDelegateHandle);
		AbilityActivatedDelegateHandle.Reset();
	}

	OwnerASC = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatComponent::Attack()
{
	TryActivateAbilityByTag(PrimaryAttackAbilityTag);
}

void UProject_JCombatComponent::BindToGAS(UAbilitySystemComponent* ASC)
{
	if (!ASC || OwnerASC == ASC)
	{
		return;
	}

	if (OwnerASC && AbilityActivatedDelegateHandle.IsValid())
	{
		OwnerASC->AbilityActivatedCallbacks.Remove(AbilityActivatedDelegateHandle);
		AbilityActivatedDelegateHandle.Reset();
	}

	OwnerASC = ASC;
	AbilityActivatedDelegateHandle = OwnerASC->AbilityActivatedCallbacks.AddUObject(this, &UProject_JCombatComponent::OnAbilityActivatedCallback);
}

void UProject_JCombatComponent::OnAbilityActivatedCallback(UGameplayAbility* Ability)
{
	if (!Ability) return;

	// Weapon presentation components can react to ability tags here without owning combat authority.
}

bool UProject_JCombatComponent::TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const
{
	if (!OwnerASC || !AbilityTag.IsValid())
	{
		return false;
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return OwnerASC->TryActivateAbilitiesByTag(AbilityTags);
}

void UProject_JCombatComponent::SetOwnedCombatStateTag(const FGameplayTag& StateTag, bool bEnabled)
{
	if (!OwnerASC || !StateTag.IsValid())
	{
		return;
	}

	if (bEnabled)
	{
		if (!OwnedLooseCombatStateTags.Contains(StateTag))
		{
			if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC))
			{
				ProjectJASC->AddProjectJLooseGameplayTag(StateTag);
			}
			else
			{
				OwnerASC->AddLooseGameplayTag(StateTag);
			}
			OwnedLooseCombatStateTags.Add(StateTag);
		}
		return;
	}

	if (OwnedLooseCombatStateTags.Remove(StateTag) > 0)
	{
		if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC))
		{
			ProjectJASC->RemoveProjectJLooseGameplayTag(StateTag);
		}
		else
		{
			OwnerASC->RemoveLooseGameplayTag(StateTag);
		}
	}
}

void UProject_JCombatComponent::ClearOwnedCombatStateTags()
{
	if (!OwnerASC)
	{
		OwnedLooseCombatStateTags.Reset();
		return;
	}

	for (const FGameplayTag& StateTag : OwnedLooseCombatStateTags)
	{
		if (UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(OwnerASC))
		{
			ProjectJASC->RemoveProjectJLooseGameplayTag(StateTag);
		}
		else
		{
			OwnerASC->RemoveLooseGameplayTag(StateTag);
		}
	}

	OwnedLooseCombatStateTags.Reset();
}

void UProject_JCombatComponent::ServerRequestSSRHit_Implementation(AActor* HitActor, float ClientTimestamp, FVector TraceStart, FVector TraceEnd)
{
	if (!HitActor) return;

	const UWorld* World = GetWorld();
	if (!World || !FMath::IsFinite(ClientTimestamp))
	{
		return;
	}

	const float RequestAge = World->GetTimeSeconds() - ClientTimestamp;
	if (RequestAge < 0.0f || RequestAge > MaxServerSideRewindRequestAge)
	{
		return;
	}

	if (UProject_JServerSideRewindComponent* SSRComp = HitActor->FindComponentByClass<UProject_JServerSideRewindComponent>())
	{
		if (SSRComp->ServerVerifyHit(ClientTimestamp, TraceStart, TraceEnd))
		{
			// SSR Verified the hit!
			// Apply damage/effects to HitActor here
		}
		else
		{
			// SSR Rejected Hit
		}
	}
	else
	{
		// Target has no SSR component, fallback to normal hit
		// Apply damage/effects to HitActor here
	}
}
