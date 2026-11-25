#pragma once

#include "CoreMinimal.h"
#include "MMO/ServicePort.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JNakamaServicePort.generated.h"

class UProject_JGatewaySubsystem;

/** 3-Mode development & production backend switch (BP01 / Blueprints README). */
UENUM(BlueprintType)
enum class EProject_JBackendRuntimeMode : uint8
{
	/** Mode 0: PIE Standalone / local tests without Docker (in-memory fenced mock + SQLite). */
	StandaloneMock,
	/** Mode 1: PIE Dedicated / local server connected to Docker Compose Nakama + PostgreSQL + Valkey. */
	LocalDockerNakama,
	/** Mode 2: Dedicated server connected to external Nakama/Gateway endpoint. */
	DedicatedExternal
};

/**
 * Bridges ProjectJ::MMO::IServicePort onto Nakama RPCs (via UProject_JGatewaySubsystem)
 * while preserving zero-dependency Mode 0 StandaloneMock execution for 1-second PIE iteration.
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_J_API UProject_JNakamaServicePortSubsystem : public UGameInstanceSubsystem, public ProjectJ::MMO::IServicePort
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ProjectJ::MMO::IServicePort
	virtual void Submit(ProjectJ::MMO::FServiceRequest Request, TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion) override;
	virtual void Cancel(const FGuid& RequestId) override;

	UFUNCTION(BlueprintCallable, Category = "MMO|Backend")
	void SetRuntimeMode(EProject_JBackendRuntimeMode NewMode) { RuntimeMode = NewMode; }

	UFUNCTION(BlueprintPure, Category = "MMO|Backend")
	EProject_JBackendRuntimeMode GetRuntimeMode() const { return RuntimeMode; }

	/** Registers a local Mode 0 mock handler for a (Service, Operation) pair. */
	void RegisterMockHandler(
		FName Service,
		FName Operation,
		TFunction<ProjectJ::MMO::FServiceResponse(const ProjectJ::MMO::FServiceRequest&)> Handler);

	/** Advances/queries monotonic session epoch fencing in Mode 0 mock storage. */
	int64 AcquireMockSessionEpoch(const FGuid& CharacterId);
	bool IsMockSessionEpochCurrent(const FGuid& CharacterId, int64 Epoch) const;

	int32 GetMockReceiptCount() const;

protected:
	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend")
	EProject_JBackendRuntimeMode RuntimeMode = EProject_JBackendRuntimeMode::StandaloneMock;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend")
	FString NakamaRpcPrefix = TEXT("/v2/rpc/");

	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend", meta = (ClampMin = "1", ClampMax = "16384"))
	int32 MaxMockReceipts = 4096;

private:
	struct FMockReceipt
	{
		FName Service;
		FName Operation;
		TArray<uint8> RequestPayload;
		ProjectJ::MMO::FServiceResponse Response;
	};

	void HandleMockSubmit(ProjectJ::MMO::FServiceRequest Request, TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion);
	void HandleGatewaySubmit(ProjectJ::MMO::FServiceRequest Request, TFunction<void(ProjectJ::MMO::FServiceResponse)> Completion);
	void HandleGatewayEnvelopeResponse(const struct FProject_JBackendResponseEnvelope& Response);

	mutable FCriticalSection MockMutex;
	TMap<TPair<FName, FName>, TFunction<ProjectJ::MMO::FServiceResponse(const ProjectJ::MMO::FServiceRequest&)>> MockHandlers;
	TMap<FGuid, FMockReceipt> MockReceipts;
	TMap<FGuid, int64> MockSessionEpochs;
	TMap<FGuid, TFunction<void(ProjectJ::MMO::FServiceResponse)>> PendingGatewayCompletions;
	TSet<FGuid> CancelledRequests;
	bool bInitialized = false;
};
