#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JDungeonInstanceSubsystem.generated.h"

class UProject_JLootTableDefinition;
class FJsonObject;

/**
 * Reusable DataAsset defining an instanced Dungeon (or future personal/guild instanced interior).
 * Assign this on AProject_JWorldPortal or register at startup to auto-wire map travel, party limits, and rewards.
 */
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JDungeonDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FName DungeonId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon")
	TSoftObjectPtr<UWorld> DungeonMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MinPartySize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MaxPartySize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon", meta = (ClampMin = "60.0"))
	float TimeLimitSeconds = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon|Reward")
	TObjectPtr<UProject_JLootTableDefinition> ClearLootTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dungeon|Quest")
	FName QuestClearEventId = NAME_None;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Both World Portal entrance and Menu UI click share one backend ticket/allocation pipeline (BP01). */
UENUM(BlueprintType)
enum class EProject_JDungeonEntrySource : uint8
{
	WorldPortal,
	MenuUI
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JReturnAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Channel")
	FName ZoneId = TEXT("MainWorld");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Channel")
	int32 ChannelId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Channel")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Channel")
	FRotator Rotation = FRotator::ZeroRotator;

	bool IsValid() const { return !ZoneId.IsNone() && ChannelId > 0 && !Location.ContainsNaN() && !Rotation.ContainsNaN(); }
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JChannelInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	FName ZoneId = TEXT("MainWorld");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	int32 ChannelId = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	FString ServerEndpoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	int32 CurrentCCU = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	int32 MaxCCU = 200;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	bool bDraining = false;

	/** UTC Unix seconds when the channel sent the heartbeat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World|Channel")
	double LastHeartbeatSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JDungeonJoinTicket
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FGuid InstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FName DungeonId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FGuid CharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FGuid Nonce;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FString ServerEndpoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	double IssuedAtSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	double ExpiresAtSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FString SignatureHex;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	bool bReturnToWorld = false;

	/** Signed destination and anchor for returning to a sibling channel. */
	UPROPERTY()
	FString SiblingReturnRoute;
	UPROPERTY()
	FName DestinationZone;
	UPROPERTY()
	int32 DestinationChannel = 0;

	bool IsValidFormat() const { return InstanceId.IsValid() && !DungeonId.IsNone() && CharacterId.IsValid() && Nonce.IsValid() && !SignatureHex.IsEmpty(); }
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JDungeonInstanceRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FGuid InstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FName DungeonId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	FString ServerEndpoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon")
	bool bCleared = false;

	double ExpiresAtSeconds = 0.0;
	TSet<FGuid> AllowedCharacters;
	TMap<FGuid, FProject_JReturnAnchor> ReturnAnchors;
	TSet<FGuid> JoinedCharacters;
	TSet<FGuid> ClaimedClearCharacters;
	TMap<FGuid, double> DisconnectGraceDeadlines; // 180s reconnect grace per BP01
	TMap<FGuid, FProject_JDungeonJoinTicket> PendingReconnectTickets;
	TMap<FGuid, FProject_JDungeonJoinTicket> PendingReturnTickets;
};

/**
 * Coordinates Open-World Channel Registry, Warm-Pool / On-Demand Dungeon Allocation,
 * Signed Single-Use Join Tickets, 180s Disconnect Grace Recovery, and ReturnAnchor Safe Return (BP01).
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_J_API UProject_JDungeonInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double DefaultTicketTTLSeconds = 60.0;
	static constexpr double DisconnectGraceSeconds = 180.0;
	static constexpr double ChannelHeartbeatTimeoutSeconds = 15.0;
	static constexpr int32 MaxActiveInstances = 512;
	static constexpr int32 FieldBossCrowdThrottleThreshold = 40;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Open-World Channel Registry ---
	void RegisterChannelHeartbeat(const FProject_JChannelInfo& Channel);
	void ReplaceZoneChannels(FName Zone, TArray<FProject_JChannelInfo> Snapshot);
	bool ResolveReturnChannelEndpoint(const FProject_JReturnAnchor& Anchor, FString& OutEndpoint, int32& OutChannelId) const;
	static bool ShouldEnableFieldBossCrowdThrottle(int32 PlayersWithin100m) { return PlayersWithin100m > FieldBossCrowdThrottleThreshold; }

	// --- Warm Pool & On-Demand Dungeon Allocation ---
	void RegisterWarmPoolServer(const FString& ServerEndpoint);
	void RegisterDungeonDefinition(const UProject_JDungeonDefinition* Definition);
	const UProject_JDungeonDefinition* FindDungeonDefinition(FName DungeonId) const;
	bool HasActiveDungeonInstance(const FGuid& CharacterId) const;
	FGuid GetCharacterInstanceId(const FGuid& CharacterId) const { return CharacterToInstance.FindRef(CharacterId); }

	/**
	 * Unified entry for both WorldPortal interaction and MenuUI click.
	 * Saves ReturnAnchor, allocates an instance (Warm Pool first), and mints signed single-use tickets.
	 */
	bool RequestDungeonEntry(
		EProject_JDungeonEntrySource EntrySource,
		FName DungeonId,
		const TArray<FGuid>& PartyCharacterIds,
		const TMap<FGuid, FProject_JReturnAnchor>& ReturnAnchors,
		double NowSeconds,
		FGuid& OutInstanceId,
		TArray<FProject_JDungeonJoinTicket>& OutTickets);

	/** Admission data for a trusted allocator/transport. The destination must install it before travel. */
	bool GetInstanceAdmission(const FGuid& InstanceId, FProject_JDungeonInstanceRecord& OutRecord) const;
	bool InstallInstanceAdmission(const FProject_JDungeonInstanceRecord& Record, double NowSeconds);
	bool TryIssueReservedJoinTicket(const FGuid& InstanceId, const FGuid& CharacterId, double NowSeconds, FProject_JDungeonJoinTicket& OutTicket) const;
	bool MakeJoinTicket(const FProject_JDungeonInstanceRecord& Record, const FGuid& CharacterId, double NowSeconds,
		FProject_JDungeonJoinTicket& OutTicket) const;
	bool HasNetworkSigningSecret() const;
	void PollOnlineWorker(UWorld* World, const FGuid& Worker, const FString& Endpoint, FName DungeonId);

	/** Verifies signature, TTL, membership, and single-use Nonce (prevents ticket replay/forgery). */
	bool VerifyAndConsumeJoinTicket(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds);
	bool CanAdmitJoinTicket(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds) const;
	static bool ParseJoinTicketOptions(const FString& Options, FProject_JDungeonJoinTicket& OutTicket);

	/** Records player disconnect inside a dungeon and opens a 180s reconnect grace window. */
	bool RecordDisconnectInDungeon(const FGuid& CharacterId, double NowSeconds);

	/** Re-issues a signed reconnect ticket if the character returns within the 180s grace window. */
	bool TryIssueReconnectTicket(const FGuid& CharacterId, double NowSeconds, FProject_JDungeonJoinTicket& OutTicket);

	/** Marks dungeon cleared and grants idempotent clear receipt per (InstanceId, CharacterId). */
	bool ClaimDungeonClearReward(const FGuid& InstanceId, const FGuid& CharacterId);
	bool ResolveFinalBossDefeat(const UProject_JDungeonDefinition& Definition);
	bool IsDungeonClearedFor(const FGuid& Character) const;

	/** Resolves the return anchor through a live channel with capacity in the same zone. */
	bool ResolveCharacterReturn(const FGuid& CharacterId, FProject_JReturnAnchor& OutAnchor, FString& OutEndpoint, int32& OutChannelId) const;
	bool GetCharacterReturnAnchor(const FGuid& CharacterId, FProject_JReturnAnchor& OutAnchor) const;
	bool TryIssueReturnTicket(const FGuid& CharacterId, double NowSeconds, FProject_JDungeonJoinTicket& OutTicket,
		bool bEmbedReturnPlacement = false);
	bool GetReturnPlacement(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds, FProject_JReturnAnchor& OutAnchor) const;
	bool FinishCharacterReturn(const FProject_JDungeonJoinTicket& Ticket);

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	void SetLocalFixtureSigningSecret(FString Secret) { TicketSigningSecret = MoveTemp(Secret); }
	void SetLocalFixtureChannel(int32 ChannelId) { LocalWorldChannelId = ChannelId; }
#endif

	/** Prunes expired nonces, instances, return anchors and any abandoned reconnect request. */
	void PruneExpiredState(double NowSeconds);

	/** Synchronized UTC Unix seconds so cross-world ticket TTL & disconnect grace never drift across world uptimes. */
	static double GetCurrentUtcEpochSeconds();

	int32 GetActiveInstanceCount() const { return ActiveInstances.Num(); }
	void SetOnlineChannelId(int32 Channel) { LocalWorldChannelId = FMath::Max(1, Channel); }

private:
	void RetireInstance(FGuid Instance);
	void InstallOnlineWorkerAdmission(const FJsonObject& Admission, const FString& WorkerId, FName DungeonId, const FString& Endpoint);
	bool EnqueueOnlineClear(UWorld* World, const TSharedRef<FJsonObject>& Clear);
	FGuid OnlineWorkerAdmission;
	FGuid OnlineWorkerId;
	bool bOnlineClearEnqueued = false;
	bool bPollingOnlineWorker = false;
	FString ComputeTicketSignature(const FProject_JDungeonJoinTicket& Ticket) const;
	bool ReadSiblingReturnRoute(const FString& Route, FProject_JReturnAnchor& OutAnchor) const;

	UPROPERTY(Config)
	FString TicketSigningSecret = TEXT("ProjectJ_LocalDev_TicketSecret_ChangeInProd");

	UPROPERTY(Config)
	int32 LocalWorldChannelId = 1;

	TMap<TPair<FName, int32>, FProject_JChannelInfo> Channels;
	TArray<FString> WarmPoolEndpoints;
	TMap<FName, TWeakObjectPtr<const UProject_JDungeonDefinition>> DungeonDefinitions;
	TMap<FGuid, FProject_JDungeonInstanceRecord> ActiveInstances;
	TMap<FGuid, FGuid> CharacterToInstance;
	TMap<FGuid, double> ConsumedTicketNonces; // Nonce -> ExpiresAtSeconds for TTL pruning
	int32 NextDynamicPort = 7780;
};
