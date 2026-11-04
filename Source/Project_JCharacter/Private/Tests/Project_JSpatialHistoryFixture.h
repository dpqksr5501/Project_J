#pragma once

#include "Components/ActorComponent.h"
#include "Backends/MoverBackendLiaison.h"
#include "GameFramework/GameStateBase.h"
#include "Project_JSpatialHistoryFixture.generated.h"

/** Exercises the GT success-delivery contract, without starting an async simulation. */
UCLASS(Transient, NotBlueprintable)
class UProject_JSpatialHistoryAsyncLiaison : public UActorComponent, public IMoverBackendLiaisonInterface
{
	GENERATED_BODY()
public:
	virtual double GetCurrentSimTimeMs() override { return 0; }
	virtual int32 GetCurrentSimFrame() override { return 0; }
	virtual bool IsAsync() const override { return true; }
};

/** An absolute clock seam; the engine's float offset is not a large-time test clock. */
UCLASS(Transient, NotBlueprintable)
class AProject_JSpatialHistoryClock : public AGameStateBase
{
	GENERATED_BODY()
public:
	double TestServerTime = 0.0;
	virtual double GetServerWorldTimeSeconds() const override { return TestServerTime; }
};
