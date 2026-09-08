// Copyright Project J. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JEquipmentClientTestComponent.generated.h"

class AProject_JPlayerState;
struct FStreamableHandle;

/** Opt-in PIE fixture. Equipment mutations still use the production inventory-ID RPCs. */
UCLASS()
class UProject_JEquipmentClientTestComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JEquipmentClientTestComponent();
	void Execute(const FString& Action);
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend class FProjectJEquipmentClientFixtureTest;
	bool IsTestWorld() const;
	AProject_JPlayerState* GetTestPlayerState() const;
	void Cleanup();
	void LogState(const TCHAR* Stage) const;

	UFUNCTION(Server, Reliable)
	void ServerPrepare();
	UFUNCTION(Server, Reliable)
	void ServerStop();
	UFUNCTION(Server, Reliable)
	void ServerInspect();
	UFUNCTION(Client, Reliable)
	void ClientPrepared(FGuid InstanceId);

	FGuid ClientItemId;
	FGuid CreatedItemId;
	TWeakObjectPtr<AProject_JPlayerState> CreatedFor;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FTimerHandle ExpiryTimer;
	uint32 Generation = 0;
	double LastPrepareTime = -100.0;
	double LastInspectTime = -100.0;
	bool bPreparing = false;
};
