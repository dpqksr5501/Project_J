#pragma once

#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerController.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Project_JOnlineLoginFixture.generated.h"

class UProject_JPlayerHUDWidget;

UCLASS(NotBlueprintable)
class AProject_JOnlineLoginFixtureController : public AProject_JPlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void AcknowledgePossession(APawn* InPawn) override;
	UFUNCTION(Client, Reliable) void ClientFinishOnlineDungeon();

private:
	UFUNCTION() void ObserveConnection(bool bDispatched);
	UFUNCTION() void ObserveMailbox(const TArray<FProject_JInboxEntry>& Entries, FGuid Next, bool bSuccess);
	UFUNCTION() void ObserveAction(FName Action, bool bSuccess, FName Reason);
	void VerifyInventory();
	void OpenMailbox();
	FTimerHandle Probe;
	FGuid ExpectedItem;
	TWeakObjectPtr<UProject_JPlayerHUDWidget> TestHUD;
	bool bSplitRequested = false;
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

private:
	void AdvanceDungeonProbe();
	TWeakObjectPtr<AProject_JOnlineLoginFixtureController> DungeonPlayer;
	FTimerHandle DungeonProbe;
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
	TSet<FName> HarvestedTestNodes;
};
