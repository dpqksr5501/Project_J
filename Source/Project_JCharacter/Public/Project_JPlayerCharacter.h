// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Project_JBaseCharacter.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"
#include "Network/Project_JHandoverSerializable.h"
#include "Project_JPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UProject_JCameraComponent;
class UInputAction;
class UAnimMontage;
class UProject_JCombatComponent;
class UProject_JCharacterAnimProfile;
class UProject_JLocomotionAnimStateComponent;
class UProject_JMotionMatchingAssetSet;
class UProject_JMotionMatchingTrajectoryComponent;
class UProject_JLocomotionProfile;
class UProject_JWeaponAnimProfile;
class UProject_JCombatAnimProfile;
class UProject_JCharacterViewModel;
class UProject_JCharacterUIBindingComponent;
class UProject_JReplicatedAnimEventComponent;
class UProject_JReplicatedJumpStateComponent;
class UProject_JCombatStateComponent;
class UProject_JCombatIntroComponent;
class UProject_JInventoryComponent;
class UProject_JSkillInputExecutionComponent;
class UProject_JSkillInputRouterComponent;
class UProject_JMountComponent;
class UAbilitySystemComponent;
struct FGameplayTag;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

USTRUCT(BlueprintType)
struct FProject_JPlayerHandoverSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	uint8 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handover")
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 *  A player-controllable third person character
 *  Implements a controllable orbiting camera and motion matching landing logic.
 */
UCLASS(abstract)
class PROJECT_JCHARACTER_API AProject_JPlayerCharacter : public AProject_JBaseCharacter, public IProject_JHandoverSerializable
{
	GENERATED_BODY()

	friend class UProject_JPlayerInputBindingComponent;

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom = nullptr;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera = nullptr;

	/** Camera Component handling boom, zoom, and multi-player optimization */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCameraComponent> CameraComponent = nullptr;

	/** Job-specific combat component (dynamically cached at runtime) */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatComponent> ActiveCombatComponent = nullptr;

	/** Owns locomotion animation state consumed by AnimInstance and Chooser Tables. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JLocomotionAnimStateComponent> LocomotionAnimStateComponent = nullptr;

	/** Native trajectory buffer used by PoseSearch motion matching. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JMotionMatchingTrajectoryComponent> MotionMatchingTrajectoryComponent = nullptr;
	
	/** ViewModel for UI bindings */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCharacterUIBindingComponent> CharacterUIBindingComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JPlayerInputBindingComponent> PlayerInputBindingComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JSkillInputRouterComponent> SkillInputRouterComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JSkillInputExecutionComponent> SkillInputExecutionComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Replication", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JReplicatedAnimEventComponent> ReplicatedAnimEventComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Replication", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JReplicatedJumpStateComponent> ReplicatedJumpStateComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatStateComponent> CombatStateComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatIntroComponent> CombatIntroComponent = nullptr;

	/** Replicated player-side state for possession-based mounts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JMountComponent> MountComponent = nullptr;

	/** The one world mount currently summoned from this player's mount item. */
	UPROPERTY(Replicated, Transient, VisibleAnywhere, BlueprintReadOnly, Category="Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class AProject_JMountCharacter> SummonedMount = nullptr;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> JumpAction = nullptr;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction = nullptr;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> LookAction = nullptr;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MouseLookAction = nullptr;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SprintAction = nullptr;

	/** Toggle Combat Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ToggleCombatAction = nullptr;

	/** Attack Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> AttackAction = nullptr;

	/** Heavy Attack Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> HeavyAttackAction = nullptr;

	/** Skill modifier Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SkillModifierAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> InteractAction = nullptr;

public:

	/** Constructor */
	AProject_JPlayerCharacter();	

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual AActor* GetAbilitySystemOwnerActor() const override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void Tick(float DeltaTime) override;
	virtual void OnJumped_Implementation() override;

protected:

	void StopMoveInput();

	// 기본 착지 이벤트 오버라이드(모션매칭)
	virtual void Landed(const FHitResult& Hit) override;

	void PlayCombatIntroMontage();

	void CancelCombatIntroMontage();
	void RefreshAbilitySystemDependentComponents();

	UFUNCTION()
	void OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void RegisterCombatStateTagEvents();
	void UnregisterCombatStateTagEvents();
	void ApplyCombatRotationMode(bool bEnableCombatRotation);
	bool HasCombatStateTag(const FGameplayTag& StateTag) const;
	bool TryActivateAbilityByTag(const FGameplayTag& AbilityTag);
	void CancelAbilitiesByTag(const FGameplayTag& AbilityTag);
	bool IsCombatActionBlockingSprint() const;
	bool ShouldAllowSprintInCombat() const;

public:
	UFUNCTION()
	void OnCombatStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
protected:

	void ApplyLocomotionProfile();
	void LogAnimationProfileConfiguration() const;
	void UpdateMaxWalkSpeed();
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
	void DispatchFallOffStartAnimationEvent();
	void DispatchLandingCancelAnimationEvent();

	// C++에서 '진짜 착지'로 판정되었을 때 블루프린트(ABP)로 신호를 보내기 위한 이벤트	
	UFUNCTION(BlueprintImplementableEvent, Category = "Movement|Animation")
	void K2_OnRealLanded();

public:

	void NotifyFallOffStartedForAnimation();

	void NotifyLandingCancelledForAnimation();

	/** Toggles the combat mode on or off */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void ToggleCombatMode();

	/** Starts combat mode after the combat intro montage finishes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void BeginCombatModeWithIntro();

	/** Stops the combat intro montage when a higher-priority reaction, such as hit react, needs to take over. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void InterruptCombatIntroForHit();

	/** Triggers the active combat component's attack */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TriggerPlayerAttack();

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleSkillInputTagPressed(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleSkillInputTagReleased(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSprint();

	/** Owner-only inventory UI calls this with its replicated item instance id. */
	UFUNCTION(BlueprintCallable, Category = "Mount")
	void RequestUseMountItem(FGuid ItemInstanceId);

	UFUNCTION(BlueprintCallable, Category="Interaction")
	void TryInteract();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** Returns ActiveCombatComponent subobject **/
	FORCEINLINE class UProject_JCombatComponent* GetActiveCombatComponent() const { return ActiveCombatComponent; }

	UProject_JCombatComponent* RefreshActiveCombatComponent();

	/** Returns LocomotionAnimStateComponent subobject **/
	FORCEINLINE class UProject_JLocomotionAnimStateComponent* GetLocomotionAnimStateComponent() const { return LocomotionAnimStateComponent; }

	/** Returns MotionMatchingTrajectoryComponent subobject **/
	FORCEINLINE class UProject_JMotionMatchingTrajectoryComponent* GetMotionMatchingTrajectoryComponent() const { return MotionMatchingTrajectoryComponent; }

	FORCEINLINE class UProject_JMountComponent* GetMountComponent() const { return MountComponent; }
	FORCEINLINE class AProject_JMountCharacter* GetSummonedMount() const { return SummonedMount; }

	class UProject_JInventoryComponent* GetInventoryComponent() const;

private:
	UFUNCTION(Server, Reliable)
	void ServerRequestUseMountItem(FGuid ItemInstanceId);

	UFUNCTION(Server, Reliable)
	void ServerTryInteract();

public:

	UFUNCTION(BlueprintPure, Category = "UI")
	UProject_JCharacterViewModel* GetCharacterViewModel() const;

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

	UFUNCTION(BlueprintPure, Category = "Movement|Ground")
	bool IsGroundStartAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Ground")
	bool IsGroundStopAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	bool IsCombatLocomotionOverlayAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	float GetEffectiveCombatAimAlpha() const;

	UFUNCTION(BlueprintCallable, Category = "Movement|Landing")
	void FinishLanding(bool bForceFinish = false);

public:
	// IProject_JHandoverSerializable Interface
	virtual void SerializeForHandover(TArray<uint8>& OutData) override;
	virtual void DeserializeFromHandover(const TArray<uint8>& InData) override;

	virtual void SetCharacterLevel(int32 NewLevel) override;

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

	/** Derived locally from replicated equipment; it is not an independent replicated state. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Weapon")
	TObjectPtr<UProject_JWeaponAnimProfile> CurrentWeaponAnimProfile = nullptr;

	// --- Combat States ---

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

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Tags")
	FGameplayTag SprintAbilityTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities|Tags")
	FGameplayTag CombatToggleAbilityTag;

private:
	bool bHadMoveInputForReplication = false;
	bool bAppliedCombatModeTag = false;
	bool bWasSprintLocomotionAllowed = false;
};
