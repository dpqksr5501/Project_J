#pragma once

#include "CoreMinimal.h"
#include "Mount/Project_JMountCharacter.h"
#include "InputAction.h"
#include "Project_JFlyingMountCharacter.generated.h"

/** Shared aerial movement rules for wyverns, griffins, and dragons. */
UCLASS(Abstract, Blueprintable)
class PROJECT_JMOUNT_API AProject_JFlyingMountCharacter : public AProject_JMountCharacter
{
	GENERATED_BODY()
public:
	AProject_JFlyingMountCharacter();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Starts the protected takeoff phase; it does not grant flight input immediately. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mount|Flight") bool BeginFlight();

	/** Requests the protected landing phase. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mount|Flight") bool EndFlight();

	/** Server-side cue used by the takeoff timer and optionally an animation event. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Mount|Flight") bool CommitTakeOffImpulse();
	UFUNCTION(BlueprintCallable, Category="Mount|Flight")
	void HandleFlightAnimationCue(EProject_JMountFlightAnimationCue Cue);
	UFUNCTION(BlueprintCallable, Category="Mount|Flight") void SetGliding(bool bNewGliding);
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsGliding() const { return bIsGliding; }
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsFlyingMount() const;
	UFUNCTION(BlueprintPure, Category="Mount|Flight") EProject_JMountFlightState GetFlightState() const { return FlightState; }
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsFlightInputLocked() const;
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsTakingOff() const { return FlightState == EProject_JMountFlightState::TakingOff || FlightState == EProject_JMountFlightState::AutoAscending; }
	UFUNCTION(BlueprintPure, Category="Mount|Flight") bool IsLanding() const { return FlightState == EProject_JMountFlightState::Landing; }
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
protected:
	void HandleMove(const struct FInputActionValue& Value);
	void HandleLook(const struct FInputActionValue& Value);
	void HandleAscend(const struct FInputActionValue& Value);
	void HandleDescend(const struct FInputActionValue& Value);
	void HandleTakeOff();
	void HandleDismount();
	UFUNCTION(Server, Reliable) void ServerRequestBeginFlight();
	UFUNCTION()
	void OnRep_FlightState(EProject_JMountFlightState PreviousState);
	UFUNCTION(BlueprintImplementableEvent, Category="Mount|Flight")
	void K2_OnFlightStateChanged(EProject_JMountFlightState PreviousState, EProject_JMountFlightState NewState);
	UFUNCTION(BlueprintImplementableEvent, Category="Mount|Flight")
	void K2_OnFlightAnimationCue(EProject_JMountFlightAnimationCue Cue);

	void SetFlightState(EProject_JMountFlightState NewState);
	bool HasVerticalClearance(float Distance) const;
	bool FindLandingSurface(FHitResult& OutHit) const;
	bool BeginLanding();
	void FinishLanding();
	void UpdateAutoAscent(float DeltaSeconds);
	void UpdateLanding(float DeltaSeconds);
	void ApplyFlightBraking(bool bGliding);
	void ApplyFlightStateTags(EProject_JMountFlightState PreviousState, EProject_JMountFlightState NewState);
	float ResolveTakeOffImpulseTime() const;
	float ResolveFlightCueTime(const class UAnimSequenceBase* Animation, EProject_JMountFlightAnimationCue Cue, float FallbackTime) const;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> MoveAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> LookAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> AscendAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> DescendAction = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Input") TObjectPtr<class UInputAction> InteractAction = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight") float FlightSpeed = 1200.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight") float GlideSpeed = 900.0f;
	/** Source sequence used to derive the authoritative takeoff cue time from its notify track. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Takeoff") TObjectPtr<class UAnimSequenceBase> TakeOffCueAnimation = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Takeoff", meta=(ClampMin="1.0")) float AutoTakeOffHeight = 450.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Takeoff", meta=(ClampMin="1.0")) float AutoAscentSpeed = 700.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Takeoff", meta=(ClampMin="1.0")) float RequiredTakeOffClearance = 500.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Landing", meta=(ClampMin="1.0")) float LandingDescentSpeed = 450.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Landing", meta=(ClampMin="0.0")) float LandingCompletionTolerance = 30.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Landing", meta=(ClampMin="0.0", ClampMax="89.0")) float MaxLandingSlopeDegrees = 35.0f;
	/** Source sequence used to derive the authoritative touchdown cue time from its notify track. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Landing") TObjectPtr<class UAnimSequenceBase> LandingCueAnimation = nullptr;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Braking", meta=(ClampMin="0.0")) float FlyingBrakingDeceleration = 2200.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Braking", meta=(ClampMin="0.0")) float GlideBrakingDeceleration = 350.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Braking", meta=(ClampMin="0.0")) float FlyingBrakingFriction = 4.5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Mount|Flight|Braking", meta=(ClampMin="0.0")) float GlideBrakingFriction = 1.0f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Mount|Flight") bool bIsGliding = false;
	UPROPERTY(ReplicatedUsing=OnRep_FlightState, BlueprintReadOnly, Category="Mount|Flight") EProject_JMountFlightState FlightState = EProject_JMountFlightState::Grounded;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Mount|Flight") float FlightPhaseStartServerTime = 0.0f;
	float AutoAscentTargetZ = 0.0f;
	float ActiveTakeOffImpulseTime = 0.45f;
	float ActiveLandingTouchdownTime = 0.0f;
	bool bLandingTouchdownConfirmed = false;
	bool bLandingTouchdownCueReached = false;
	float DefaultBrakingDecelerationFlying = 0.0f;
	float DefaultBrakingFriction = 0.0f;
	bool bTakeOffRequestPending = false;
	float TakeOffRequestExpiryTime = 0.0f;
};
