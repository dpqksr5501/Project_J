// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameModeBase.h"
#include "Project_JMMOTypes.h"
#include "MMO/Coordination.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "World/Project_JDungeonInstanceSubsystem.h"
#include "Project_JGameMode.generated.h"

class AProject_JPlayerState;
class AProject_JPlayerCharacter;
class FJsonObject;
struct FProject_JQuestProgressInput;
struct FProject_JProfileDurableResult;

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract, Config=Game)
class AProject_JGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AProject_JGameMode();

	virtual void InitGameState() override;
	virtual void BeginPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void Logout(AController* Exiting) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Native trusted-server entry, never an RPC. The adapter must already have
	 * verified Account's principal before Begin, then verify character ownership
	 * before Complete(true). Prototype PostLogin identities do not imply this.
	 * Begin must precede asynchronous ownership verification. */
	bool BeginTrustedSession(APlayerController* Player, const FProject_JAccountId& Account,
		const FProject_JCharacterId& Character, ProjectJ::MMO::FServerSessionAttempt& OutAttempt);
	bool CompleteTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt, bool bOwnershipVerified,
		ProjectJ::MMO::FServerSessionLease& OutLease);
	bool CancelTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt);
	bool RevokeTrustedSession(AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& ExpectedLease);
	bool IsTrustedSessionCurrent(const AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& Lease) const;
	bool RedeemOnlineLogin(APlayerController* Player, FGuid Ticket);
	int64 GetOnlineEpoch(const AProject_JPlayerState* Player) const;
	bool HasOnlineSnapshotEdit(FGuid Character) const { return OnlineInventoryPending.Contains(Character); }
	void PublishRecoveredQuestProgress(FGuid Character, const FJsonObject& Snapshot) const;
	int32 GetOnlineChannelId() const { return OnlineChannelId; }
	void NotifyServerDraining();
	bool IsServerDraining() const;
	bool SubmitOnlineInventoryEdit(AProject_JPlayerState* Player, const FProject_JPersistentEdit& Edit,
		FProject_JPersistentCompletion Complete = {}, TFunction<void(const FProject_JProfileDurableResult&)> OnDurable = {});
	/** Opt-in server policy: login waits without prototype identity/Pawn until a
	 * trusted adapter completes ownership verification. No verifier is supplied.
	 * After Complete, the adapter calls normal HandleStartingNewPlayer. */
	UPROPERTY(EditDefaultsOnly, Category="MMO|Session")
	bool bRequireTrustedSessions = false;
	/** Applied once during InitGameState, before login; clamped by NetDriver capacity (256 ready for source Iris). */
	UPROPERTY(Config, EditDefaultsOnly, Category="MMO|Session", meta=(ClampMin="1"))
	int32 MaxTrustedSessions = 256;
	/** Opt-in dungeon gate. A ticket is consumed only by the matching verified trusted session. */
	UPROPERTY(EditDefaultsOnly, Category="MMO|Dungeon")
	bool bRequireDungeonTicket = false;

protected:
	/** Selects the avatar from the server's committed progression on initial spawn and respawn. */
	UPROPERTY(EditDefaultsOnly, Category="MMO|Character")
	TMap<FName, TSubclassOf<AProject_JPlayerCharacter>> PlayerPawnClassesByJob;

	virtual FProject_JWorldInstanceId CreatePrototypeWorldInstanceId() const;
	virtual void AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const;

private:
	friend class AProject_JPlayerState;
	void PlayerSessionEnded(AProject_JPlayerState* Player);
	struct FPendingSession
	{
		ProjectJ::MMO::FServerSessionAttempt Attempt;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<AProject_JPlayerState> State;
	};
	struct FBoundSession
	{
		ProjectJ::MMO::FServerSessionLease Lease;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<AProject_JPlayerState> State;
	};
	void RetireSession(const FBoundSession& Binding);
	bool IsSessionPlayer(const APlayerController* Player, const AProject_JPlayerState* State) const;
	bool CanBindSessionIdentity(const APlayerController* Player, const AProject_JPlayerState* State,
		const FGuid& Account, const FGuid& Character) const;
	ProjectJ::MMO::FServerSessionGate SessionGate;
	TMap<FGuid, FPendingSession> PendingSessions;
	TMap<FGuid, FBoundSession> BoundSessions;
	TMap<TWeakObjectPtr<APlayerController>, FProject_JDungeonJoinTicket> PendingDungeonTickets;
	struct FPendingDungeonReturn
	{
		FProject_JDungeonJoinTicket Ticket;
		FProject_JReturnAnchor Anchor;
	};
	TMap<TWeakObjectPtr<APlayerController>, FPendingDungeonReturn> PendingDungeonReturns;
	UPROPERTY(Config, EditDefaultsOnly, Category="MMO|Online")
	FString OnlineAdvertisedEndpoint;
	UPROPERTY(Config, EditDefaultsOnly, Category="MMO|Online", meta=(ClampMin="1"))
	int32 OnlineChannelId = 1;
	/** A dedicated worker advertises a single authored dungeon on this endpoint. */
	UPROPERTY(Config, EditDefaultsOnly, Category="MMO|Online")
	FName OnlineDungeonId;
	FGuid OnlineIncarnation;
	FTSTicker::FDelegateHandle OnlineHeartbeat;
	double NextOnlineHeartbeat = 0.;
	TSet<TWeakObjectPtr<APlayerController>> OnlineLoginPending;
	TMap<TWeakObjectPtr<const AProject_JPlayerState>, int64> OnlineEpochs;
	TMap<FGuid, TWeakObjectPtr<AProject_JPlayerState>> OnlineInventoryPending;
	void ReleaseOnlineSnapshotEdit(FGuid Character, TWeakObjectPtr<AProject_JPlayerState> Player);
	int32 ChannelCapacity = 0;
	void PublishOnlineHeartbeat();
	bool InstallOnlineLogin(APlayerController* Player, const FJsonObject& Response);
	bool SubmitOnlineSnapshotEdit(AProject_JPlayerState* Player, FName Route, const TSharedRef<FJsonObject>& Body, FGuid Key,
		FProject_JPersistentCompletion Complete, TFunction<void(const FProject_JProfileDurableResult&)> OnDurable = {});
	bool SubmitOnlineQuestProgress(AProject_JPlayerState* Player, TConstArrayView<FProject_JQuestProgressInput> Inputs);
	bool bSessionsClosing = false;
};



