#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JPresentationBudgetSubsystem.generated.h"

class UProject_JWeaponPresentationComponent;
struct FProjectJPresentationBudgetStats
{
	uint64 Applied = 0, Coalesced = 0, Rejected = 0;
	int32 Pending = 0, LastApplications = 0;
	double LastMilliseconds = 0, MaxQueueMilliseconds = 0;
};

/** World-local GT budget for remote cosmetic weapon creation. Gameplay and notify-critical work bypass it. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JPresentationBudgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxPending = 4096;
	static constexpr int32 MaxApplications = 4;
	bool Request(UProject_JWeaponPresentationComponent* Component, uint64 Revision);
	void Cancel(UProject_JWeaponPresentationComponent* Component);
	const FProjectJPresentationBudgetStats& GetStats() const { return Stats; }
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Deinitialize() override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	struct FRequest { uint64 Revision; double QueuedAt; uint64 Admission; };
	struct FOrderEntry { TWeakObjectPtr<UProject_JWeaponPresentationComponent> Component; uint64 Admission; };
	TMap<TWeakObjectPtr<UProject_JWeaponPresentationComponent>, FRequest> Requests;
	TArray<FOrderEntry> Order;
	uint64 NextAdmission = 0;
	int32 Head = 0;
	bool bStopped = false, bTicking = false;
	FProjectJPresentationBudgetStats Stats;
};
