// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JNPCCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/SkeletalMeshComponent.h"

AProject_JNPCCharacter::AProject_JNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// In Stage 5, NPC characters must construct their own local GAS/Equipment components.
	AbilitySystemComponent = CreateDefaultSubobject<UProject_JAbilitySystemComponent>(TEXT("NPCAbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UProject_JAttributeSet>(TEXT("NPCAttributeSet"));
	EquipmentManager = CreateDefaultSubobject<UProject_JEquipmentManagerComponent>(TEXT("NPCEquipmentManager"));
}

void AProject_JNPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bApplyDefaultNPCOptimizationPolicy)
	{
		ApplyDefaultNPCOptimizationPolicy();
	}
}

void AProject_JNPCCharacter::ApplyDefaultNPCOptimizationPolicy()
{
	SetNetCullDistanceSquared(FMath::Square(NPCNetCullDistance));
	SetNetUpdateFrequency(NPCNetUpdateFrequency);
	SetMinNetUpdateFrequency(NPCMinNetUpdateFrequency);

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->bEnableUpdateRateOptimizations = true;
		MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
}

EProject_JNPCUpdateBudgetTier AProject_JNPCCharacter::GetNPCUpdateBudgetTier() const
{
	if (CurrentSignificance >= 3.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Hidden;
	}
	if (CurrentSignificance >= 2.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Far;
	}
	if (CurrentSignificance >= 1.0f)
	{
		return EProject_JNPCUpdateBudgetTier::Mid;
	}
	return EProject_JNPCUpdateBudgetTier::Near;
}

float AProject_JNPCCharacter::GetRecommendedAIUpdateInterval() const
{
	return GetRecommendedAIUpdateIntervalForTier(GetNPCUpdateBudgetTier());
}

EProject_JNPCUpdateBudgetTier AProject_JNPCCharacter::GetDecisionTierForDistance(double Distance, EProject_JNPCUpdateBudgetTier PreviousTier) const
{
	const double Gates[] = {SignificanceNearDistance, SignificanceMidDistance, SignificanceFarDistance};
	if (!FMath::IsFinite(Distance) || Distance < 0 || !FMath::IsFinite(Gates[0]) || !FMath::IsFinite(Gates[1])
		|| !FMath::IsFinite(Gates[2]) || Gates[0] <= 0 || Gates[1] <= Gates[0] || Gates[2] <= Gates[1])
	{
		return EProject_JNPCUpdateBudgetTier::Near;
	}
	int32 Tier = 0;
	for (int32 Boundary = 0; Boundary < 3; ++Boundary)
	{
		// Promotion is immediate; demotion requires 10% extra distance to avoid boundary oscillation.
		const double Gate = Gates[Boundary] * (Boundary >= static_cast<int32>(PreviousTier) ? 1.1 : 1.0);
		if (Distance < Gate) { break; }
		Tier = Boundary + 1;
	}
	return static_cast<EProject_JNPCUpdateBudgetTier>(Tier);
}

float AProject_JNPCCharacter::GetRecommendedAIUpdateIntervalForTier(EProject_JNPCUpdateBudgetTier Tier) const
{
	switch (Tier)
	{
	case EProject_JNPCUpdateBudgetTier::Mid:
		return NPCUpdateBudget.MidUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Far:
		return NPCUpdateBudget.FarUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Hidden:
		return NPCUpdateBudget.HiddenUpdateInterval;
	case EProject_JNPCUpdateBudgetTier::Near:
	default:
		return NPCUpdateBudget.NearUpdateInterval;
	}
}
