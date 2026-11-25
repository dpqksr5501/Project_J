#pragma once

#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Project_JLocalDungeonFixture.generated.h"

class UProject_JMMOClientBridgeComponent;

UCLASS(NotBlueprintable)
class AProject_JLocalDungeonFixtureController : public APlayerController
{
	GENERATED_BODY()

public:
	AProject_JLocalDungeonFixtureController();
	UFUNCTION(Client, Reliable)
	void ClientFinishDungeonFixture();
	UFUNCTION(Client, Reliable)
	void ClientDisconnectDungeonFixture();

	UPROPERTY()
	TObjectPtr<UProject_JMMOClientBridgeComponent> MMO;
};

/** Explicitly selected account and allocator fixture for a loopback-only Editor test. */
UCLASS(NotBlueprintable)
class AProject_JLocalDungeonFixture : public AProject_JGameMode
{
	GENERATED_BODY()

public:
	AProject_JLocalDungeonFixture();
	virtual void BeginPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

private:
	void StopFixture();
	FString RunDirectory;
	int32 SourcePort = 0;
	int32 DestinationPort = 0;
	int32 SiblingPort = 0;
	bool bEnabled = false;
	bool bReady = false;
	bool bOutbound = false;
	bool bDestination = false;
	bool bSibling = false;
	bool bTestReconnect = false;
	bool bAwaitingReconnect = false;
	bool bIssuedReconnect = false;
	FGuid Account, Character, Allocation;
	FTimerHandle Watchdog;
};
