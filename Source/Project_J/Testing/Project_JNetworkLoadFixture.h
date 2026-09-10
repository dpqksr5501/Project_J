#pragma once
#include "CoreMinimal.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Project_JNPCCharacter.h"
#include "Project_JNetworkLoadFixture.generated.h"

UCLASS()
class UProject_JNetworkFixtureLifetime : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	FTSTicker::FDelegateHandle Timeout;
};

/** Native diagnostics only. Selected explicitly by CLI map URL and -ProjectJNetworkFixture. */
UCLASS(NotBlueprintable)
class AProject_JNetworkViewPawn : public APawn
{
	GENERATED_BODY()
public:
	AProject_JNetworkViewPawn();
};

UCLASS(NotBlueprintable)
class AProject_JNetworkProbeNPC : public AProject_JNPCCharacter
{
	GENERATED_BODY()
public:
	AProject_JNetworkProbeNPC();
	UPROPERTY(Replicated) int32 ProbeId = 0;
	UPROPERTY(Replicated) int32 Pulse = 0;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

UCLASS(NotBlueprintable)
class AProject_JAlwaysRelevantProbeNPC : public AProject_JNetworkProbeNPC
{
	GENERATED_BODY()
public:
	AProject_JAlwaysRelevantProbeNPC();
};

UCLASS(NotBlueprintable)
class AProject_JNetworkLoadController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void PlayerTick(float DeltaSeconds) override;
	UFUNCTION(Client, Reliable) void ClientConfigure(int32 Stage, int32 Slot, const TArray<int32>& Expected, FGuid OwnItem, bool bExpectItems, int32 MinimumPulse);
	UFUNCTION(Server, Reliable) void ServerObserve(int32 Stage, const TArray<int32>& Seen, bool bOwnInventory, bool bForeignInventoryEmpty, bool bEquipment, int32 MinimumPulse);
	UFUNCTION(Client, Reliable) void ClientFinish(bool bSuccess);
	int32 FixtureSlot = INDEX_NONE;
	int32 ConfirmedStage = INDEX_NONE;
	FString LastObservation;
private:
	int32 CurrentStage = INDEX_NONE, LocalSlot = INDEX_NONE, RequiredPulse = 0;
	FGuid ItemId;
	bool bItemsExpected = true;
	TArray<int32> ExpectedIds;
	double NextObservation = 0;
};

UCLASS(NotBlueprintable)
class AProject_JNetworkLoadGameMode : public AProject_JGameMode
{
	GENERATED_BODY()
public:
	AProject_JNetworkLoadGameMode();
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void Tick(float DeltaSeconds) override;
	bool ValidateObservation(AProject_JNetworkLoadController* PC, int32 Stage, const TArray<int32>& Seen, bool bOwn, bool bForeignEmpty, bool bEquipment, int32 MinPulse);
private:
	void StartStage(int32 NewStage);
	void Finish(bool bSuccess, const FString& Reason);
	TArray<int32> ExpectedIds(int32 Slot) const;
	UPROPERTY() TArray<TObjectPtr<AProject_JNetworkLoadController>> Clients;
	UPROPERTY() TArray<TObjectPtr<AProject_JNetworkProbeNPC>> NPCs;
	TArray<FGuid> Items;
	int32 Stage = 0, Pulse = 0, RequiredPulse = 0;
	double Started = 0, StageStarted = 0, NextPulse = 0;
	uint32 StageOutBytes = 0;
	uint64 RegionId = 0;
	bool bFixture = false, bAllRelevant = false, bFinished = false, bSawDisconnect = false;
	FString Rows = TEXT("stage,seconds,connections,out_bytes\n");
};
