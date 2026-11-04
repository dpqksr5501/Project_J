#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCommandBudgetComponent.generated.h"

DECLARE_DELEGATE_RetVal(bool, FProject_JCommandAuthority);

/** PlayerState lifetime budget shared by client command entry points, across Avatar changes.
 * Domain limits/ownership checks still apply. Trusted server mutations never consume it.
 * Process-local overload control, not account authentication or a cross-server quota.
 */
UCLASS(ClassGroup=(ProjectJ))
class PROJECT_JCORE_API UProject_JCommandBudgetComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JCommandBudgetComponent();
	static bool AdmitClientCommand(AActor* Source);
	/** Domain throttle in addition to the shared session budget. Trusted server work bypasses this entry point. */
	static bool AdmitClientCommand(AActor* Source, double& NextAllowedTime, double MinimumIntervalSeconds);
	bool TryConsume();
	/** Native owner-bound session fence; unbound preserves prototype/NPC fixtures. */
	FProject_JCommandAuthority CommandAuthority;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	int64 GetRejectedCount() const { return RejectedCount; }

	UPROPERTY(EditDefaultsOnly, Category="Network", meta=(ClampMin="1", ClampMax="10000"))
	int32 CommandsPerSecond = 80;
private:
	bool bClosed = false;
	double WindowStart = -1;
	int32 Used = 0;
	int64 RejectedCount = 0;
};
