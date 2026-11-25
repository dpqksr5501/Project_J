#include "AI/Project_JThreatComponent.h"
#include "Project_JBaseCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "Project_JGameplayTags.h"

UProject_JThreatComponent::UProject_JThreatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JThreatComponent::AddDamageThreat(
	AActor* Source,
	const FGuid& CharacterId,
	double Damage,
	double SkillThreatMultiplier,
	double NowSeconds)
{
	check(IsInGameThread());
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || Owner->IsActorBeingDestroyed() || bIsEvading
		|| !IsValid(Source) || Source->GetWorld() != Owner->GetWorld() || !FMath::IsFinite(Damage) || !FMath::IsFinite(SkillThreatMultiplier)
		|| Damage <= 0.0 || SkillThreatMultiplier <= 0.0)
	{
		return;
	}
	const double Added = Damage * SkillThreatMultiplier;
	if (!FMath::IsFinite(Added)) return;
	ActiveThreat.FindOrAdd(Source) += Added;

	if (CharacterId.IsValid())
	{
		FProject_JContributionRecord& Rec = ContributionMap.FindOrAdd(CharacterId);
		Rec.CharacterId = CharacterId;
		Rec.DamageDealt += Damage;
		Rec.ParticipationSeconds += 0.5;
	}
	if (!CurrentTarget.IsValid())
	{
		CurrentTarget = Source;
	}
}

void UProject_JThreatComponent::AddHealingThreat(
	AActor* Healer,
	const FGuid& CharacterId,
	double EffectiveHeal,
	int32 EngagedMobCount,
	double NowSeconds)
{
	check(IsInGameThread());
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || Owner->IsActorBeingDestroyed() || bIsEvading
		|| !IsValid(Healer) || Healer->GetWorld() != Owner->GetWorld() || !FMath::IsFinite(EffectiveHeal) || EffectiveHeal <= 0.0)
	{
		return;
	}
	const int32 Divisor = FMath::Max(1, EngagedMobCount);
	const double Added = (EffectiveHeal * 0.45) / static_cast<double>(Divisor);
	ActiveThreat.FindOrAdd(Healer) += Added;

	if (CharacterId.IsValid())
	{
		FProject_JContributionRecord& Rec = ContributionMap.FindOrAdd(CharacterId);
		Rec.CharacterId = CharacterId;
		Rec.EffectiveHealing += EffectiveHeal;
		Rec.ParticipationSeconds += 0.5;
	}
}

void UProject_JThreatComponent::ApplyTaunt(AActor* Taunter, double DurationSeconds, double NowSeconds)
{
	if (bIsEvading || !IsValid(Taunter) || DurationSeconds <= 0.0)
	{
		return;
	}
	double Highest = 0.0;
	for (const auto& Pair : ActiveThreat)
	{
		if (Pair.Key.IsValid())
		{
			Highest = FMath::Max(Highest, Pair.Value);
		}
	}
	double& Entry = ActiveThreat.FindOrAdd(Taunter);
	Entry = FMath::Max(Entry, Highest * 1.10 + 1.0);
	ForcedTauntTarget = Taunter;
	ForcedTauntUntil = NowSeconds + DurationSeconds;
	CurrentTarget = Taunter;
}

void UProject_JThreatComponent::RemoveFromActiveAggro(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	ActiveThreat.Remove(Actor);
	if (CurrentTarget.Get() == Actor)
	{
		CurrentTarget.Reset();
	}
	if (ForcedTauntTarget.Get() == Actor)
	{
		ForcedTauntTarget.Reset();
	}
	// Note: ContributionMap is intentionally retained for field-boss personal loot fairness.
}

AActor* UProject_JThreatComponent::ResolvePrimaryTarget(const FVector& NPCLocation, double NowSeconds)
{
	if (bIsEvading)
	{
		CurrentTarget.Reset();
		return nullptr;
	}

	// Prune dead/destroyed entries.
	for (auto It = ActiveThreat.CreateIterator(); It; ++It)
	{
		const AActor* Target = It.Key().Get();
		const auto* ASC = Target ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
		if (!Target || Target->IsActorBeingDestroyed() || (ASC && ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Dead)))
		{
			It.RemoveCurrent();
		}
	}
	if (ActiveThreat.IsEmpty())
	{
		CurrentTarget.Reset();
		return nullptr;
	}

	if (ForcedTauntTarget.IsValid() && NowSeconds < ForcedTauntUntil && ActiveThreat.Contains(ForcedTauntTarget))
	{
		CurrentTarget = ForcedTauntTarget;
		return CurrentTarget.Get();
	}

	AActor* BestCandidate = nullptr;
	double BestThreat = -1.0;
	for (const auto& Pair : ActiveThreat)
	{
		if (Pair.Key.IsValid() && Pair.Value > BestThreat)
		{
			BestThreat = Pair.Value;
			BestCandidate = Pair.Key.Get();
		}
	}

	if (!CurrentTarget.IsValid() || !ActiveThreat.Contains(CurrentTarget))
	{
		CurrentTarget = BestCandidate;
		return CurrentTarget.Get();
	}

	if (BestCandidate && BestCandidate != CurrentTarget.Get())
	{
		const double CurrentThreat = ActiveThreat.FindRef(CurrentTarget);
		const double CandidateDistSq = FVector::DistSquared(NPCLocation, BestCandidate->GetActorLocation());
		const double RequiredRatio = (CandidateDistSq <= FMath::Square(MeleeRangeCm)) ? MeleeHysteresisRatio : RangedHysteresisRatio;
		if (BestThreat > CurrentThreat * RequiredRatio)
		{
			CurrentTarget = BestCandidate;
		}
	}
	return CurrentTarget.Get();
}

bool UProject_JThreatComponent::EvaluateLeashAndEvade(const FVector& CurrentLocation, float UnreachableSeconds, bool bHasValidTarget)
{
	if (bIsEvading)
	{
		return true;
	}
	const float LeashR = BrainDefinition ? BrainDefinition->LeashRadiusCm : 3500.0f;
	const float MaxZ = BrainDefinition ? BrainDefinition->MaxChaseZDeltaCm : 1200.0f;
	const float UnreachLimit = BrainDefinition ? BrainDefinition->UnreachableEvadeSeconds : 5.0f;

	const bool bOutHorizontal = FVector::DistSquared2D(CurrentLocation, HomeLocation) > FMath::Square(static_cast<double>(LeashR));
	const bool bOutVertical = FMath::Abs(CurrentLocation.Z - HomeLocation.Z) > static_cast<double>(MaxZ);
	const bool bUnreachable = UnreachableSeconds >= UnreachLimit;

	if (bOutHorizontal || bOutVertical || bUnreachable || !bHasValidTarget)
	{
		bIsEvading = true;
		ActiveThreat.Reset();
		CurrentTarget.Reset();
		ForcedTauntTarget.Reset();
		OnEvadeStarted.Broadcast();
		return true;
	}
	return false;
}

void UProject_JThreatComponent::FinishEvadeReset()
{
	bIsEvading = false;
	ActiveThreat.Reset();
	ContributionMap.Reset();
	CurrentTarget.Reset();
	ForcedTauntTarget.Reset();
	CurrentBossPhase = 1;
	if (AProject_JBaseCharacter* OwnerChar = Cast<AProject_JBaseCharacter>(GetOwner()); OwnerChar && OwnerChar->HasAuthority())
	{
		if (UProject_JAttributeSet* Attrs = OwnerChar->GetAttributeSet())
		{
			Attrs->SetHealth(Attrs->GetMaxHealth());
		}
	}
}

int32 UProject_JThreatComponent::EvaluateBossPhase(float CurrentHealth, float MaxHealth)
{
	if (MaxHealth <= 0.0f)
	{
		return CurrentBossPhase;
	}
	const float Ratio = FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f);
	static const TArray<float> DefaultThresholds = { 0.70f, 0.35f };
	const TArray<float>& Thresholds = (BrainDefinition && !BrainDefinition->BossPhaseHealthThresholds.IsEmpty())
		? BrainDefinition->BossPhaseHealthThresholds
		: DefaultThresholds;

	// Count thresholds crossed regardless of authoring order (unsorted DataAsset arrays still produce monotonic phases).
	int32 CrossedCount = 0;
	for (const float Threshold : Thresholds)
	{
		if (FMath::IsFinite(Threshold) && Ratio <= Threshold)
		{
			++CrossedCount;
		}
	}
	const int32 DesiredPhase = 1 + CrossedCount;
	if (DesiredPhase > CurrentBossPhase)
	{
		const int32 OldPhase = CurrentBossPhase;
		CurrentBossPhase = DesiredPhase;
		OnBossPhaseChanged.Broadcast(OldPhase, CurrentBossPhase);
	}
	return CurrentBossPhase;
}

TArray<FGuid> UProject_JThreatComponent::GetEligibleContributors(double BossMaxHealth, double MinFraction) const
{
	check(IsInGameThread());
	TArray<FGuid> Out;
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !FMath::IsFinite(BossMaxHealth) || BossMaxHealth <= 0.0
		|| !FMath::IsFinite(MinFraction)) return Out;
	const double Threshold = FMath::Max(1.0, BossMaxHealth * FMath::Clamp(MinFraction, 0.0, 1.0));
	for (const auto& Pair : ContributionMap)
	{
		const double Score = Pair.Value.TotalScore();
		if (Pair.Key.IsValid() && FMath::IsFinite(Score) && Score >= Threshold)
		{
			Out.Add(Pair.Key);
		}
	}
	return Out;
}

double UProject_JThreatComponent::GetThreatValue(const AActor* Actor) const
{
	if (!Actor)
	{
		return 0.0;
	}
	return ActiveThreat.FindRef(const_cast<AActor*>(Actor));
}
