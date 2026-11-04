#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JPresentationBudgetSubsystem.generated.h"

class UActorComponent;
class UProject_JWeaponPresentationComponent;
class UProject_JCombatPresentationComponent;
struct FProjectJPresentationBudgetStats
{
	uint64 Applied = 0, Coalesced = 0, Rejected = 0;
	int32 Pending = 0, LastApplications = 0;
	uint64 Frame = MAX_uint64, ImmediateApplications = 0, ImmediateRejected = 0;
	int32 FrameApplications = 0, FrameVisits = 0, PeakFrameApplications = 0;
	// Queued apply time only. Immediate one-shots share the hard count, not this soft timer.
	double FrameQueueMilliseconds = 0;
	double LastMilliseconds = 0, MaxQueueMilliseconds = 0;
};

/** Shared world-local GT attempt budget for queued remote weapons/loops and immediate remote one-shots.
 * Credits reset only on a real engine frame change; not an active-emitter, load or GPU budget.
 * Local/viewed and authored exact-timing cue exceptions remain immediate. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JPresentationBudgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxPending = 4096;
	static constexpr int32 MaxApplications = 4;
	bool Request(UProject_JWeaponPresentationComponent* Component, uint64 Revision);
	bool Request(UProject_JCombatPresentationComponent* Component, uint64 Revision);
	/** Acquire this instant or drop it; live queued work has priority. No reservation/refund or late replay. */
	bool TryAcquireOneShot(UProject_JCombatPresentationComponent* Component);
	void Cancel(UProject_JWeaponPresentationComponent* Component);
	void Cancel(UProject_JCombatPresentationComponent* Component);
	const FProjectJPresentationBudgetStats& GetStats() const { return Stats; }
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Deinitialize() override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	void RefreshFrameBudget();
	bool RequestComponent(UActorComponent* Component, uint64 Revision);
	void CancelComponent(UActorComponent* Component);
	struct FRequest { uint64 Revision; double QueuedAt; uint64 Admission; };
	struct FOrderEntry { TWeakObjectPtr<UActorComponent> Component; uint64 Admission; };
	TMap<TWeakObjectPtr<UActorComponent>, FRequest> Requests;
	TArray<FOrderEntry> Order;
	uint64 NextAdmission = 0;
	int32 Head = 0;
	bool bStopped = false, bTicking = false;
	FProjectJPresentationBudgetStats Stats;
};
