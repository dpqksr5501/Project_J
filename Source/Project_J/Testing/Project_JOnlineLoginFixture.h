#pragma once

#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerController.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Project_JOnlineLoginFixture.generated.h"

class UProject_JPlayerHUDWidget;
class AProject_JPlayerState;

UCLASS(NotBlueprintable)
class AProject_JOnlineLoginFixtureController : public AProject_JPlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* InPawn) override;
	UFUNCTION(Client, Reliable) void ClientFinishOnlineDungeon();
	UFUNCTION(Client, Reliable) void ClientAdvanceOnlineChatFixture(FName Step);

private:
	UFUNCTION() void ObserveConnection(bool bDispatched);
	UFUNCTION() void ObserveQueuePosition(int32 Position);
	UFUNCTION() void ObserveMailbox(const TArray<FProject_JInboxEntry>& Entries, FGuid Next, bool bSuccess);
	UFUNCTION() void ObserveAction(FName Action, bool bSuccess, FName Reason);
	UFUNCTION() void ObserveChatFixture(FGuid SenderId, EProject_JChatChannel Channel, const FString& Message);
	UFUNCTION(Server, Reliable) void ServerReportOnlineChatFixture(FName Event);
	void VerifyInventory();
	bool VerifyQuestReward();
	void OpenMailbox();
	FTimerHandle Probe;
	FGuid ExpectedItem;
	FGuid ChatTestTarget;
	bool bCompletedUnblock = false;
	FGuid ChatFixtureSender;
	bool bChatFixtureReceiver = false;
	FGuid QuestHistoryRequest;
	TWeakObjectPtr<UProject_JPlayerHUDWidget> TestHUD;
	bool bSplitRequested = false;
	bool bQuestClaimRequested = false;
};

UCLASS(NotBlueprintable)
class AProject_JOnlineLoginFixture : public AProject_JGameMode
{
	GENERATED_BODY()
public:
	AProject_JOnlineLoginFixture();
	virtual void BeginPlay() override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	void AdvanceOnlineChatFixture(AProject_JOnlineLoginFixtureController& Player, FName Event);

private:
	void AdvanceDungeonProbe();
	void PrepareOnlineQuest(AProject_JPlayerState& State);
	TWeakObjectPtr<AProject_JOnlineLoginFixtureController> DungeonPlayer;
	TWeakObjectPtr<AProject_JOnlineLoginFixtureController> ChatSender;
	TWeakObjectPtr<AProject_JOnlineLoginFixtureController> ChatReceiver;
	FName ChatFixturePhase;
	int32 ChatFixtureLogouts = 0;
	int32 RemainingPublicVisits = 0;
	FTimerHandle DungeonProbe;
	FTimerHandle OnlineQuestProbe;
	UPROPERTY()
	TObjectPtr<UProject_JDungeonDefinition> FixtureDungeon;
	FVector FixtureAnchor = FVector::ZeroVector;
	bool bDungeonSource = false;
	bool bDungeonDestination = false;
	bool bDungeonOutbound = false;
	bool bDungeonReturned = false;
	bool bAuthoredSandbox = false;
	bool bCraftDispatched = false;
	bool bBossDefeatIssued = false;
	bool bFieldDefeatIssued = false;
	TSet<FName> HarvestedTestNodes;
};
