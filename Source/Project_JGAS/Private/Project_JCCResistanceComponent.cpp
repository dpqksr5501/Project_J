#include "Project_JCCResistanceComponent.h"

UProject_JCCResistanceComponent::UProject_JCCResistanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UProject_JCCResistanceComponent::TryApplyCC(
	EProject_JCCCategory Category,
	float BaseDurationSeconds,
	double NowSeconds,
	bool bHasSuperArmor,
	bool bIsInvulnerable)
{
	if (!FMath::IsFinite(BaseDurationSeconds) || !FMath::IsFinite(NowSeconds) || BaseDurationSeconds <= 0.0f || bIsInvulnerable)
	{
		return 0.0f;
	}
	if (bHasSuperArmor && Category != EProject_JCCCategory::Slow)
	{
		return 0.0f;
	}
	if (bIsBossCCImmune && Category != EProject_JCCCategory::Slow)
	{
		return 0.0f;
	}

	FDRBucket& Bucket = DRState.FindOrAdd(Category);
	if (NowSeconds - Bucket.LastEndOrApplyTime >= static_cast<double>(DRWindowSeconds))
	{
		Bucket.Applications = 0;
	}

	float Scale = 0.0f;
	switch (Bucket.Applications)
	{
	case 0: Scale = 1.0f; break;
	case 1: Scale = 0.5f; break;
	case 2: Scale = 0.25f; break;
	default: Scale = 0.0f; break;
	}

	if (Scale <= 0.0f)
	{
		return 0.0f;
	}

	++Bucket.Applications;
	const float EffectiveDuration = BaseDurationSeconds * Scale;
	Bucket.LastEndOrApplyTime = NowSeconds + static_cast<double>(EffectiveDuration);
	return EffectiveDuration;
}

bool UProject_JCCResistanceComponent::ApplyStaggerDamage(float StaggerDamage, double NowSeconds)
{
	if (!bIsBossCCImmune || !FMath::IsFinite(StaggerDamage) || !FMath::IsFinite(NowSeconds)
		|| MaxStagger <= 0.0f || StaggerDamage <= 0.0f || IsGroggy(NowSeconds))
	{
		return false;
	}
	CurrentStagger = FMath::Max(0.0f, CurrentStagger - StaggerDamage);
	if (CurrentStagger <= 0.0f)
	{
		GroggyEndTime = NowSeconds + static_cast<double>(GroggyDurationSeconds);
		CurrentStagger = MaxStagger;
		OnBossStaggered.Broadcast(GroggyDurationSeconds);
		return true;
	}
	return false;
}

void UProject_JCCResistanceComponent::ResetAll()
{
	DRState.Reset();
	CurrentStagger = MaxStagger;
	GroggyEndTime = -1000.0;
}
