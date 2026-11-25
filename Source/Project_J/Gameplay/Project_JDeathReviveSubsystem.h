#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "Project_JDeathReviveSubsystem.generated.h"

class AProject_JBaseCharacter;

UENUM(BlueprintType)
enum class EProject_JRevivePath : uint8
{
	Graveyard,
	HealerSkill,
	ReviveItem
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JGraveyardPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Revive")
	FName GraveyardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Revive")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Revive")
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * Server-authoritative Death & Revive lifecycle built on Phase 0's State_Dead pipeline (BP05).
 * Enforces action locking while dead, 3 revive paths, 3s post-revive invulnerability,
 * 180s corpse auto-release, and dungeon boss-fight graveyard lock.
 */
UCLASS()
class PROJECT_J_API UProject_JDeathReviveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double CorpseAutoReleaseSeconds = 180.0;
	static constexpr double PostReviveInvulnerabilitySeconds = 3.0;
	static constexpr double HealerOfferTimeoutSeconds = 30.0;

	virtual void Deinitialize() override;

	void RegisterGraveyard(const FProject_JGraveyardPoint& Point);
	void SetDungeonBossFightActive(bool bActive);

	/** Records authoritative death timestamp for auto-release tracking. */
	void NotifyCharacterDied(AProject_JBaseCharacter* Character, double NowSeconds);
	void NotifyCharacterRevived(AProject_JBaseCharacter* Character);

	/** Offers a healer revive that expires after 30s. */
	bool OfferHealerRevive(AProject_JBaseCharacter* DeadCharacter, const FVector& ReviveLocation, double NowSeconds);

	/** Executes an authoritative revive via Graveyard, HealerSkill, or ReviveItem. */
	bool ExecuteRevive(AProject_JBaseCharacter* Character, EProject_JRevivePath Path, double NowSeconds, FVector& OutReviveLocation);

	/** Returns true when a dead character has exceeded the 180s corpse wait limit. */
	bool ShouldAutoReleaseCorpse(const AProject_JBaseCharacter* Character, double NowSeconds) const;

	/** Returns true while the character holds the 3s post-revive invulnerability shield. */
	bool HasPostReviveInvulnerability(const AProject_JBaseCharacter* Character, double NowSeconds) const;

	/** Server-side action gate: rejects combat/mount/trade/portal commands while dead. */
	static bool CanCharacterPerformAliveAction(const AProject_JBaseCharacter* Character);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type == EWorldType::Game || Type == EWorldType::PIE; }

private:
	friend class FProjectJCorpseEndPlayTrackingTest;
	UFUNCTION()
	void OnTrackedCharacterEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	void UpdateAutoReleaseTimer();
	void ProcessCorpseReleases();
	struct FHealerOffer
	{
		FVector Location = FVector::ZeroVector;
		double ExpiresAt = 0.0;
	};
	TArray<FProject_JGraveyardPoint> Graveyards;
	TMap<TWeakObjectPtr<AProject_JBaseCharacter>, double> DeathTimes;
	TMap<TWeakObjectPtr<AProject_JBaseCharacter>, FHealerOffer> PendingHealerOffers;
	TSet<TWeakObjectPtr<AProject_JBaseCharacter>> ActiveRevives;
	FTimerHandle CorpseReleaseTimer;
	double ScheduledReleaseDeadline = TNumericLimits<double>::Max();
	uint64 LastOfferPruneFrame = MAX_uint64;
	bool bDungeonBossFightActive = false;
	bool bProcessingCorpseReleases = false;
};
