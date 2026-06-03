// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JBaseCharacter.h"
#include "Logging/LogMacros.h"
#include "Project_JPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UAnimMontage;
struct FInputActionValue;
class UProject_JCombatComponent;
class UProject_JCharacterAnimProfile;
class UProject_JLocomotionAnimStateComponent;
class UProject_JMotionMatchingAssetSet;
class UProject_JMotionMatchingTrajectoryComponent;
class UProject_JLocomotionProfile;
class UProject_JWeaponAnimProfile;
class UProject_JCombatAnimProfile;
class UProject_JCharacterViewModel;
struct FGameplayTag;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

USTRUCT()
struct FProject_JReplicatedAnimEventState
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 MoveStartCounter = 0;

	UPROPERTY()
	bool bMoveStartWasSprinting = false;

	UPROPERTY()
	uint8 MoveStopCounter = 0;

	UPROPERTY()
	uint8 JumpStartCounter = 0;

	UPROPERTY()
	uint8 FallOffStartCounter = 0;

	UPROPERTY()
	uint8 LandingCancelCounter = 0;
};

/**
 *  A player-controllable third person character
 *  Implements a controllable orbiting camera and motion matching landing logic.
 */
UCLASS(abstract)
class PROJECT_JCHARACTER_API AProject_JPlayerCharacter : public AProject_JBaseCharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

	/** Job-specific combat component (dynamically cached at runtime) */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UProject_JCombatComponent* ActiveCombatComponent;

	/** Owns locomotion animation state consumed by AnimInstance and Chooser Tables. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UProject_JLocomotionAnimStateComponent* LocomotionAnimStateComponent;

	/** Native trajectory buffer used by PoseSearch motion matching. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UProject_JMotionMatchingTrajectoryComponent* MotionMatchingTrajectoryComponent;
	
	/** ViewModel for UI bindings */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta = (AllowPrivateAccess = "true"))
	UProject_JCharacterViewModel* CharacterViewModel;

	/** Helper to update ViewModel from Attribute Set */
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);
	void OnMaxHealthChanged(const struct FOnAttributeChangeData& Data);
	void OnManaChanged(const struct FOnAttributeChangeData& Data);
	void OnMaxManaChanged(const struct FOnAttributeChangeData& Data);
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SprintAction;

	/** Toggle Combat Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* ToggleCombatAction;

	/** Attack Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AttackAction;

public:

	/** Constructor */
	AProject_JPlayerCharacter();	

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void Tick(float DeltaTime) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	void StopMoveInput();

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	// 기본 착지 이벤트 오버라이드(모션매칭)
	virtual void Landed(const FHitResult& Hit) override;

	void PlayCombatIntroMontage();

	void CancelCombatIntroMontage();

	UFUNCTION()
	void OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void ApplyCombatRotationMode(bool bEnableCombatRotation);
	void ApplyCombatModeState(bool bNewCombatMode);
	bool HasCombatStateTag(const FGameplayTag& StateTag) const;
	bool IsCombatActionBlockingSprint() const;
	bool ShouldAllowSprintInCombat() const;

	void ApplyLocomotionProfile();
	void LogAnimationProfileConfiguration() const;
	void UpdateMaxWalkSpeed();
	void ApplySprintState(bool bNewIsSprinting);
	void ApplySprintAnimationState();
	float GetEffectiveWalkSpeed() const;
	float GetEffectiveSprintSpeed() const;
	float GetEffectiveWalkRotationRateYaw() const;
	float GetEffectiveSprintRotationRateYaw() const;
	UAnimMontage* GetEffectiveCombatIntroMontage() const;
	float GetEffectiveCombatIntroMontagePlayRate() const;
	bool ShouldPlayCombatIntroMontage() const;
	bool ShouldUseCombatRotationMode() const;
	bool ShouldInterruptCombatIntroOnHit() const;
	float GetMoveInputDeadZoneForAnimation() const;
	void UpdateMoveStartReplicationState(const FVector2D& MoveInput);
	void ResetMoveStartReplicationState();
	void DispatchMoveStartAnimationEvent(bool bWasSprintingForStart);
	void DispatchMoveStopAnimationEvent();
	void DispatchJumpStartAnimationEvent();
	void DispatchFallOffStartAnimationEvent();
	void DispatchLandingCancelAnimationEvent();

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewIsSprinting);

	UFUNCTION(Server, Reliable)
	void ServerSetCombatMode(bool bNewCombatMode);

	UFUNCTION(Server, Reliable)
	void ServerTriggerPlayerAttack();

	UFUNCTION(Server, Unreliable)
	void ServerNotifyMoveStarted(bool bWasSprintingForStart);

	UFUNCTION(Server, Unreliable)
	void ServerNotifyMoveStopped();

	UFUNCTION(Server, Unreliable)
	void ServerNotifyJumpStarted();

	UFUNCTION(Server, Unreliable)
	void ServerNotifyFallOffStarted();

	UFUNCTION(Server, Unreliable)
	void ServerNotifyLandingCancelled();

	UFUNCTION()
	void OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState);

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void OnRep_CombatMode();

	UFUNCTION()
	void OnRep_CurrentWeaponAnimProfile();

	// C++에서 '진짜 착지'로 판정되었을 때 블루프린트(ABP)로 신호를 보내기 위한 이벤트	
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement|Animation")
	void K2_OnRealLanded();

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	void NotifyFallOffStartedForAnimation();

	void NotifyLandingCancelledForAnimation();

	/** Toggles the combat mode on or off */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ToggleCombatMode();

	/** Sets the combat mode to the specified state */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void SetCombatMode(bool bInCombatMode);

	/** Starts combat mode after the combat intro montage finishes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void BeginCombatModeWithIntro();

	/** Stops the combat intro montage when a higher-priority reaction, such as hit react, needs to take over. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void InterruptCombatIntroForHit();

	/** Triggers the active combat component's attack */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TriggerPlayerAttack();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSprint();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Returns ActiveCombatComponent subobject **/
	FORCEINLINE class UProject_JCombatComponent* GetActiveCombatComponent() const { return ActiveCombatComponent; }

	/** Returns LocomotionAnimStateComponent subobject **/
	FORCEINLINE class UProject_JLocomotionAnimStateComponent* GetLocomotionAnimStateComponent() const { return LocomotionAnimStateComponent; }

	/** Returns MotionMatchingTrajectoryComponent subobject **/
	FORCEINLINE class UProject_JMotionMatchingTrajectoryComponent* GetMotionMatchingTrajectoryComponent() const { return MotionMatchingTrajectoryComponent; }

	const UProject_JLocomotionProfile* GetLocomotionProfile() const;
	const UProject_JMotionMatchingAssetSet* GetMotionMatchingAssetSet() const;
	const UProject_JWeaponAnimProfile* GetWeaponAnimProfile() const;
	const UProject_JCombatAnimProfile* GetCombatAnimProfile() const;

	UFUNCTION(BlueprintCallable, Category = "Animation|Weapon")
	void SetCurrentWeaponAnimProfile(UProject_JWeaponAnimProfile* InWeaponAnimProfile);

	UFUNCTION(BlueprintPure, Category = "Animation|Weapon")
	UProject_JWeaponAnimProfile* GetCurrentWeaponAnimProfile() const { return CurrentWeaponAnimProfile.Get(); }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCombatModeActive() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAttacking() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDodging() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHitReacting() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprintLocomotionAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Jump")
	bool IsJumpLocomotionAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	float GetEffectiveCombatAimAlpha() const;

	UFUNCTION(BlueprintCallable, Category = "Movement|Landing")
	void FinishLanding(bool bForceFinish = false);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Profile Fallbacks", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback used only when no CharacterAnimProfile or LocomotionProfile provides movement speed."))
	float WalkSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Profile Fallbacks", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback used only when no CharacterAnimProfile or LocomotionProfile provides sprint speed."))
	float SprintSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Profile Fallbacks", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback used only when no CharacterAnimProfile or LocomotionProfile provides walk rotation rate."))
	float WalkRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Profile Fallbacks", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback used only when no CharacterAnimProfile or LocomotionProfile provides sprint rotation rate."))
	float SprintRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Profile", meta = (ToolTip = "Preferred top-level animation profile. New characters should assign this and leave the migration fallback fields empty."))
	TObjectPtr<UProject_JCharacterAnimProfile> CharacterAnimProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Profile|Migration Fallbacks", AdvancedDisplay, meta = (ToolTip = "Fallback locomotion profile used only when CharacterAnimProfile does not provide one."))
	TObjectPtr<UProject_JLocomotionProfile> LocomotionProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Profile|Migration Fallbacks", AdvancedDisplay, meta = (ToolTip = "Fallback asset set used only when no effective LocomotionProfile provides one."))
	TObjectPtr<UProject_JMotionMatchingAssetSet> MotionMatchingAssetSet = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponAnimProfile, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UProject_JWeaponAnimProfile> CurrentWeaponAnimProfile = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting, VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bIsSprinting = false;

	// --- Combat States ---

	/** True if the character is currently in combat mode (weapon drawn) */
	UPROPERTY(ReplicatedUsing = OnRep_CombatMode, VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsCombatMode = false;

	/** True if the character is currently attacking */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsAttacking = false;

	/** True if the character is currently dodging */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsDodging = false;

	/** True if the character is currently reacting to a hit */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bIsHitReacting = false;

	/** Upper-body montage played when entering combat mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	UAnimMontage* CombatIntroMontage = nullptr;

	/** Playback rate for CombatIntroMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float CombatIntroMontagePlayRate = 1.0f;

	/** True while the combat intro montage is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bIsPlayingCombatIntro = false;

	/** True while combat mode is waiting for the intro montage to finish. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bPendingCombatModeFromIntro = false;

	/** If true, hit reactions can stop the combat intro montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Animation")
	bool bInterruptCombatIntroOnHit = true;

private:
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAnimEvents)
	FProject_JReplicatedAnimEventState ReplicatedAnimEvents;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCombatIntroMontage = nullptr;

	bool bHadMoveInputForReplication = false;
	bool bAppliedCombatModeTag = false;
	bool bWasSprintLocomotionAllowed = false;
};
