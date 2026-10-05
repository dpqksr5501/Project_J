#pragma once

#include "Mount/Project_JFlyingMountCharacter.h"
#include "Animation/Project_JFlyingMountAnimInstance.h"
#include "Project_JMountRefinementFixtures.generated.h"

/** Native, asset-independent automation fixtures. Never registered or spawned by gameplay. */
UCLASS(NotBlueprintable, Transient)
class AProject_JMountRefinementFixture : public AProject_JFlyingMountCharacter
{
	GENERATED_BODY()
public:
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override { ++InputSetupCount; Super::SetupPlayerInputComponent(Input); }
	void SetTestInputComponent(UInputComponent* Input) { InputComponent = Input; }
	void NotifyTestClientRestart() { PawnClientRestart(); }
	void SetTestFlightActions(UInputAction* Move, UInputAction* Look, UInputAction* Ascend, UInputAction* Descend)
	{
		MoveAction = Move; LookAction = Look; AscendAction = Ascend; DescendAction = Descend;
	}
	int32 InputSetupCount = 0;
	void SetTestAllowAirDismount(bool bAllow) { bAllowAirDismount = bAllow; }
	void SetTestFlightState(EProject_JMountFlightState State) { SetFlightState(State); }
	void SetTestRole(ENetRole InRole) { SetRole(InRole); RefreshFlightTickEnabled(); }
	void ReceiveTestFlightState(EProject_JMountFlightState State)
	{
		const EProject_JMountFlightState PreviousState = FlightState;
		FlightState = State;
		OnRep_FlightState(PreviousState);
	}
	void SetTestPendingRequest(float Expiry)
	{
		bTakeOffRequestPending = true;
		TakeOffRequestExpiryTime = Expiry;
		RefreshFlightTickEnabled();
	}
	void SetTestKeepTick(bool bEnabled) { bKeepActorTickEnabled = bEnabled; RefreshFlightTickEnabled(); }
	void SetTestBlueprintTick(bool bImplemented) { bHasBlueprintTick = bImplemented; RefreshFlightTickEnabled(); }
	void SetTestMoveAction(UInputAction* Action) { MoveAction = Action; }
	void SetTestInteractAction(UInputAction* Action) { InteractAction = Action; }
	void NotifyTestControllerChanged() { OnRep_Controller(); }
	void NotifyTestUnPossessed() { UnPossessed(); }
	void UnrelatedInput() {}
	void SetTestInitialHealth(float InitialHealth, float InitialMaximum) { Health = InitialHealth; MaxHealth = InitialMaximum; }
	void StepTestLanding(float Delta) { UpdateLanding(Delta); }
	void SetTestLandingLimits(float Stall, float Duration) { LandingStallTimeout = Stall; MaxLandingDuration = Duration; }
	int32 HealthDepletionCount = 0;
	bool bRejectedReentrantDamage = false;
protected:
	virtual void HandleHealthDepleted() override
	{
		++HealthDepletionCount;
		bRejectedReentrantDamage = !ApplyMountDamage(1.0f);
		Super::HandleHealthDepleted();
	}
};

UCLASS(NotBlueprintable, Transient)
class UProject_JMountAnimRefinementFixture : public UProject_JFlyingMountAnimInstance
{
	GENERATED_BODY()
};
