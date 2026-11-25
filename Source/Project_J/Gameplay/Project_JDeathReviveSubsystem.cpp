#include "Gameplay/Project_JDeathReviveSubsystem.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JBaseCharacter.h"
#include "Project_JGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

void UProject_JDeathReviveSubsystem::Deinitialize()
{
	if (auto* World = GetWorld()) World->GetTimerManager().ClearTimer(CorpseReleaseTimer);
	ScheduledReleaseDeadline = TNumericLimits<double>::Max();
	LastOfferPruneFrame = MAX_uint64;
	Graveyards.Reset();
	for (const auto& Death : DeathTimes)
	{
		if (auto* Character = Death.Key.Get()) Character->OnEndPlay.RemoveDynamic(this, &ThisClass::OnTrackedCharacterEndPlay);
	}
	DeathTimes.Reset();
	PendingHealerOffers.Reset();
	ActiveRevives.Reset();
	Super::Deinitialize();
}

void UProject_JDeathReviveSubsystem::RegisterGraveyard(const FProject_JGraveyardPoint& Point)
{
	if (!Point.GraveyardId.IsNone() && !Point.Location.ContainsNaN() && !Point.Rotation.ContainsNaN())
	{
		if (FProject_JGraveyardPoint* Existing = Graveyards.FindByPredicate(
			[&](const FProject_JGraveyardPoint& Entry) { return Entry.GraveyardId == Point.GraveyardId; }))
		{
			*Existing = Point;
		}
		else
		{
			Graveyards.Add(Point);
		}
		UpdateAutoReleaseTimer();
	}
}

void UProject_JDeathReviveSubsystem::SetDungeonBossFightActive(bool bActive)
{
	if (bDungeonBossFightActive == bActive) return;
	bDungeonBossFightActive = bActive;
	UpdateAutoReleaseTimer();
}

void UProject_JDeathReviveSubsystem::NotifyCharacterDied(AProject_JBaseCharacter* Character, double NowSeconds)
{
	if (IsValid(Character) && Character->HasAuthority() && Character->GetWorld() == GetWorld() && FMath::IsFinite(NowSeconds))
	{
		// Coalesce stale offer cleanup to at most once per frame so a 200-player boss wipe stays O(N) instead of O(N^2).
		if (LastOfferPruneFrame != GFrameCounter)
		{
			LastOfferPruneFrame = GFrameCounter;
			for (auto It = PendingHealerOffers.CreateIterator(); It; ++It)
				if (!It.Key().IsValid() || It.Value().ExpiresAt <= NowSeconds) It.RemoveCurrent();
		}
		const double DeathTime = DeathTimes.FindOrAdd(Character, NowSeconds);
		Character->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnTrackedCharacterEndPlay);
		PendingHealerOffers.Remove(Character);
		const double CandidateDeadline = DeathTime + CorpseAutoReleaseSeconds;
		const UWorld* World = GetWorld();
		const bool bTimerActive = World && World->GetTimerManager().IsTimerActive(CorpseReleaseTimer);
		if (!bTimerActive || CandidateDeadline < ScheduledReleaseDeadline)
		{
			UpdateAutoReleaseTimer();
		}
	}
}

void UProject_JDeathReviveSubsystem::NotifyCharacterRevived(AProject_JBaseCharacter* Character)
{
	if (!Character) return;
	Character->OnEndPlay.RemoveDynamic(this, &ThisClass::OnTrackedCharacterEndPlay);
	PendingHealerOffers.Remove(Character);
	double DeathTime = 0.0;
	if (DeathTimes.RemoveAndCopyValue(Character, DeathTime) && !bProcessingCorpseReleases
		&& DeathTime + CorpseAutoReleaseSeconds <= ScheduledReleaseDeadline)
	{
		UpdateAutoReleaseTimer();
	}
}

void UProject_JDeathReviveSubsystem::OnTrackedCharacterEndPlay(AActor* Actor, EEndPlayReason::Type)
{
	NotifyCharacterRevived(Cast<AProject_JBaseCharacter>(Actor));
}

void UProject_JDeathReviveSubsystem::UpdateAutoReleaseTimer()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client) return;
	auto& Timers = World->GetTimerManager();
	Timers.ClearTimer(CorpseReleaseTimer);
	ScheduledReleaseDeadline = TNumericLimits<double>::Max();
	if (Graveyards.IsEmpty() || bDungeonBossFightActive) return;

	double NextDeadline = TNumericLimits<double>::Max();
	for (auto It = DeathTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
		else NextDeadline = FMath::Min(NextDeadline, It.Value() + CorpseAutoReleaseSeconds);
	}
	if (NextDeadline == TNumericLimits<double>::Max()) return;
	ScheduledReleaseDeadline = NextDeadline;
	const float Delay = static_cast<float>(FMath::Clamp(NextDeadline - World->GetTimeSeconds(), 1.0, CorpseAutoReleaseSeconds));
	Timers.SetTimer(CorpseReleaseTimer, this, &ThisClass::ProcessCorpseReleases, Delay, false);
}

void UProject_JDeathReviveSubsystem::ProcessCorpseReleases()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || bDungeonBossFightActive) return;
	const double Now = World->GetTimeSeconds();
	TArray<TWeakObjectPtr<AProject_JBaseCharacter>> Due;
	for (auto It = DeathTimes.CreateIterator(); It; ++It)
	{
		auto* Character = It.Key().Get();
		if (!Character || Character->IsActorBeingDestroyed() || CanCharacterPerformAliveAction(Character))
		{
			if (Character) Character->OnEndPlay.RemoveDynamic(this, &ThisClass::OnTrackedCharacterEndPlay);
			It.RemoveCurrent();
		}
		else if (It.Value() + CorpseAutoReleaseSeconds <= Now) Due.Add(It.Key());
	}
	{
		TGuardValue<bool> Processing(bProcessingCorpseReleases, true);
		for (const auto& Entry : Due)
		{
			FVector Location;
			if (auto* Character = Entry.Get(); Character && ShouldAutoReleaseCorpse(Character, Now))
				ExecuteRevive(Character, EProject_JRevivePath::Graveyard, Now, Location);
		}
	}
	UpdateAutoReleaseTimer();
}

bool UProject_JDeathReviveSubsystem::OfferHealerRevive(AProject_JBaseCharacter* DeadCharacter, const FVector& ReviveLocation, double NowSeconds)
{
	if (!IsValid(DeadCharacter) || !DeadCharacter->HasAuthority() || DeadCharacter->GetWorld() != GetWorld()
		|| !FMath::IsFinite(NowSeconds) || CanCharacterPerformAliveAction(DeadCharacter) || ReviveLocation.ContainsNaN())
	{
		return false;
	}
	FHealerOffer Offer;
	Offer.Location = ReviveLocation;
	Offer.ExpiresAt = NowSeconds + HealerOfferTimeoutSeconds;
	PendingHealerOffers.Add(DeadCharacter, Offer);
	return true;
}

bool UProject_JDeathReviveSubsystem::ExecuteRevive(
	AProject_JBaseCharacter* Character,
	EProject_JRevivePath Path,
	double NowSeconds,
	FVector& OutReviveLocation)
{
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || !Character->HasAuthority() || Character->GetWorld() != GetWorld()
		|| !FMath::IsFinite(NowSeconds) || ActiveRevives.Contains(Character) || CanCharacterPerformAliveAction(Character))
	{
		return false;
	}

	if (Path == EProject_JRevivePath::Graveyard && bDungeonBossFightActive)
	{
		// Per BP05: disallow mid-boss graveyard release inside dungeon boss fights.
		return false;
	}

	FVector TargetLocation;
	FRotator TargetRotation = Character->GetActorRotation();
	float RestoreFraction;

	if (Path == EProject_JRevivePath::HealerSkill)
	{
		const FHealerOffer* Offer = PendingHealerOffers.Find(Character);
		if (!Offer || NowSeconds > Offer->ExpiresAt)
		{
			return false;
		}
		TargetLocation = Offer->Location;
		RestoreFraction = 0.6f;
	}
	else if (Path == EProject_JRevivePath::Graveyard && !Graveyards.IsEmpty())
	{
		double BestDistSq = TNumericLimits<double>::Max();
		for (const FProject_JGraveyardPoint& Pt : Graveyards)
		{
			const double DistSq = FVector::DistSquared(Character->GetActorLocation(), Pt.Location);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				TargetLocation = Pt.Location;
				TargetRotation = Pt.Rotation;
			}
		}
		RestoreFraction = 0.75f;
	}
	else return false; // Item revives require an authoritative item-consumption path.

	const TStrongObjectPtr<AProject_JBaseCharacter> KeepCharacter(Character);
	const TStrongObjectPtr<UProject_JAttributeSet> Attrs(Character->GetAttributeSet());
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(Cast<UProject_JAbilitySystemComponent>(Character->GetAbilitySystemComponent()));
	if (!Attrs.IsValid() || !ASC.IsValid() || ASC->GetAvatarActor() != Character || !(Attrs->GetMaxHealth() > 0.f))
	{
		return false;
	}

	ActiveRevives.Add(Character);
	ON_SCOPE_EXIT { ActiveRevives.Remove(Character); };
	if (!Character->TeleportTo(TargetLocation, TargetRotation, false, false) || Character->IsActorBeingDestroyed()
		|| ASC->GetAvatarActor() != Character || Character->GetAttributeSet() != Attrs.Get()) return false;
	ASC->ProtectAvatarHealthLoss(Character, NowSeconds + PostReviveInvulnerabilitySeconds);
	NotifyCharacterRevived(Character);
	// Setting Health > 0 automatically removes replicated State_Dead via Phase 0 PostAttributeChange hook.
	Attrs->SetHealth(FMath::Max(1.0f, Attrs->GetMaxHealth() * RestoreFraction));
	Attrs->SetMana(Attrs->GetMaxMana() * RestoreFraction);
	OutReviveLocation = Character->GetActorLocation();
	return true;
}

bool UProject_JDeathReviveSubsystem::ShouldAutoReleaseCorpse(const AProject_JBaseCharacter* Character, double NowSeconds) const
{
	if (!IsValid(Character) || CanCharacterPerformAliveAction(Character))
	{
		return false;
	}
	const double* DiedAt = DeathTimes.Find(const_cast<AProject_JBaseCharacter*>(Character));
	return DiedAt && (NowSeconds - *DiedAt) >= CorpseAutoReleaseSeconds;
}

bool UProject_JDeathReviveSubsystem::HasPostReviveInvulnerability(const AProject_JBaseCharacter* Character, double NowSeconds) const
{
	if (!IsValid(Character))
	{
		return false;
	}
	const auto* ASC = Cast<UProject_JAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	return ASC && ASC->IsAvatarHealthLossProtected(Character, NowSeconds);
}

bool UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(const AProject_JBaseCharacter* Character)
{
	if (!IsValid(Character))
	{
		return false;
	}
	if (const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
	{
		if (ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Dead))
		{
			return false;
		}
	}
	if (const UProject_JAttributeSet* Attrs = Character->GetAttributeSet())
	{
		if (Attrs->GetHealth() <= 0.0f)
		{
			return false;
		}
	}
	return true;
}
