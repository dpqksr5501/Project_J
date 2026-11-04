#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JCraftingPrototypeFixture.generated.h"
class AProject_JCraftingStation;
class AProject_JPlayerState;
class UProject_JItemDefinition;
class UProject_JCraftingRecipeDefinition;
class UProject_JCharacterDataSubsystem;
class UProject_JLocalProfileSubsystem;
class AProject_JPlayerCharacter;
/** Explicit, bounded owned-map prototype. Never grants items in an ordinary game. */
UCLASS(NotBlueprintable)
class AProject_JCraftingPrototypeFixture : public AActor
{
	GENERATED_BODY()
public:
	AProject_JCraftingPrototypeFixture();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
	void Finish(bool Success, const TCHAR* Reason);
	void AdvanceReconnect(AProject_JPlayerCharacter* Avatar, UProject_JLocalProfileSubsystem* Profiles);
	void AppendReconnectRow(const TCHAR* Label, int32 HealthCount, int32 ManaCount, bool bRepeatAdmitted = false);
	enum class EReconnectMode : uint8 { None, SeedPending, RestoreCollect };
	EReconnectMode ReconnectMode = EReconnectMode::None;
	FGuid ReconnectRun, ReconnectAccount, ReconnectCharacter, ReconnectJob;
	FString ReconnectWitnessPath;
	int64 ReconnectRevision = 0, ReconnectStartedUtc = 0, ReconnectDueUtc = 0;
	uint32 ReconnectSeedPid = 0;
	int32 ReconnectValueBytes = 0;
	bool bReconnectRestored = false, bReconnectComplete = false, bReconnectConfirmed = false, bReconnectFirst = false;
	FString ReconnectRows = TEXT("phase,pid,seed_pid,run_token,account,character,profile_revision,health,mana,jobs,job_id,started_utc_ticks,due_utc_ticks,value_bytes,restored_exact,profile_pending,complete_observed,complete_confirmed,first_application,repeat_admitted,recipe_resolved,passed\n");
	void AdvanceTimedUI(AProject_JPlayerCharacter* Avatar, UProject_JLocalProfileSubsystem* Profiles, int32 HealthCount, int32 ManaCount);
	bool RestoreTimedRecipe();
	UPROPERTY(Transient) TObjectPtr<UProject_JCraftingRecipeDefinition> TimedRecipe;
	TWeakObjectPtr<UProject_JCharacterDataSubsystem> TimedData;
	TArray<TSoftObjectPtr<UProject_JCraftingRecipeDefinition>> OriginalRecipeSources;
	FName OriginalBenchRecipe; int32 OriginalBenchVersion = 1;
	bool bTimedUI = false, bRecipeInjected = false, bTimedWindowOpen = false;
	bool bTimedScreenshotRequested = false, bTimedScreenshotProcessed = false;
	FDelegateHandle TimedScreenshotHandle;
	double TimedWarmAt = 0;
	FGuid TimedObservedJob;
	FString TimedRows = TEXT("phase,health,mana,jobs,presented,remaining,request_stage,pending_observed,passed\n");
	UPROPERTY(Transient) TObjectPtr<AProject_JCraftingStation> Bench;
	UPROPERTY(Transient) TObjectPtr<AProject_JPlayerState> Player;
	UPROPERTY(Transient) TObjectPtr<UProject_JItemDefinition> Health;
	UPROPERTY(Transient) TObjectPtr<UProject_JItemDefinition> Mana;
	FString Output;
	double Started = 0;
	int64 Revision = 0;
	int32 Phase = 0;
	bool bFinished = false;
};
