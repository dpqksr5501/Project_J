// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
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
class UAnimInstance;
class UProject_JCharacterAnimProfile;
class UProject_JLocomotionAnimStateComponent;
class UProject_JMotionMatchingAssetSet;
class UProject_JMotionMatchingTrajectoryComponent;
class UProject_JLocomotionProfile;
class UProject_JWeaponAnimProfile;
class UProject_JWeaponPresentationProfile;
class UProject_JEquipmentItemDefinition;
class UProject_JCombatAnimProfile;
class UProject_JCombatStyleDefinition;
class UProject_JCharacterViewModel;
class UProject_JCharacterUIBindingComponent;
class UProject_JReplicatedAnimEventComponent;
class UProject_JReplicatedJumpStateComponent;
class UProject_JAnimationUpdateCoordinatorComponent;
class UProject_JCombatStateComponent;
class UProject_JCombatIntroComponent;
class UProject_JCombatAnimationLayerComponent;
class UProject_JCombatHitValidationComponent;
class UProject_JWeaponPresentationComponent;
class UProject_JInventoryComponent;
class UProject_JSkillInputExecutionComponent;
class UProject_JSkillInputRouterComponent;
class UProject_JMountComponent;
class UAbilitySystemComponent;
struct FGameplayTag;

DECLARE_LOG_CATEGORY_EXTERN(LogProjectJPlayer, Log, All);

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

	/** Owns short URO bypass windows shared by all replicated animation boundaries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Optimization", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JAnimationUpdateCoordinatorComponent> AnimationUpdateCoordinator = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Replication", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JReplicatedJumpStateComponent> ReplicatedJumpStateComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatStateComponent> CombatStateComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatIntroComponent> CombatIntroComponent = nullptr;

	/** Generic runtime linker for job/weapon Anim Layers; it does not depend on BP_Player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatAnimationLayerComponent> CombatAnimationLayerComponent = nullptr;

	/** Shared runtime weapon actor management. Job data selects actor and socket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JWeaponPresentationComponent> WeaponPresentationComponent = nullptr;

	/** Shared server-side rewind validation for all player jobs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JCombatHitValidationComponent> CombatHitValidationComponent = nullptr;

	/** Replicated player-side state for possession-based mounts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JMountComponent> MountComponent = nullptr;

	/** The one world mount currently summoned from this player's mount item. */
	UPROPERTY(Replicated, Transient, VisibleAnywhere, BlueprintReadOnly, Category="Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class AProject_JMountCharacter> SummonedMount = nullptr;

	/**
	 * Optional linked AnimBP used to override the master's mounted locomotion
	 * layer only while this avatar is riding. It must implement the Animation
	 * Layer Interface selected by ABP_Player's Mounted linked-layer node.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Layers", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<UAnimInstance> MountedAnimationLayerClass;

protected:
	/**
	 * Prototype-only bootstrap for a job Blueprint. The server equips this item
	 * only when the replicated Weapon slot is empty. Persistent characters should
	 * receive equipment from inventory/backend loadout restoration instead.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Prototype", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JEquipmentItemDefinition> PrototypeStartingWeapon = nullptr;


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
	void ApplyPrototypeStartingEquipment();

	// 기본 착지 이벤트 오버라이드(모션매칭)
	virtual void Landed(const FHitResult& Hit) override;

	void PlayCombatIntroMontage();
	void PlayCombatOutroMontage();
	void PlayCosmeticCombatIntroMontage();
	void ApplyCombatPresentationState(bool bCombatModeActive, bool bPlayRemoteTransitionMontage);

	void CancelCombatIntroMontage();
	void CancelCombatOutroMontage();
	void RefreshAbilitySystemDependentComponents();

	UFUNCTION()
	void OnMountChangedForAnimation(AProject_JMountCharacter* PreviousMount, AProject_JMountCharacter* NewMount);

	void RefreshMountedAnimationLayer();
	void UnlinkMountedAnimationLayer();

	UFUNCTION()
	void OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void OnCombatOutroMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void RegisterCombatStateTagEvents();
	void UnregisterCombatStateTagEvents();
	void ApplyCombatRotationMode(bool bEnableCombatRotation);
	bool HasCombatStateTag(const FGameplayTag& StateTag) const;
	bool TryActivateAbilityByTag(const FGameplayTag& AbilityTag);
	void CancelAbilitiesByTag(const FGameplayTag& AbilityTag);
	bool IsCombatActionBlockingSprint() const;
	bool ShouldRequestSprintAbility() const;
	void RefreshSprintAbilityFromInput();

public:
	UFUNCTION()
	void OnCombatStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	/** Receives the server-authoritative combat presentation state on simulated proxies. */
	UFUNCTION()
	void OnRep_ReplicatedCombatModePresentation();

	/** Receives the cosmetic draw transition before the authoritative combat tag is applied. */
	UFUNCTION()
	void OnRep_ReplicatedCombatIntroPresentation();

	UFUNCTION()
	void OnRep_CurrentCombatStyle();

	UFUNCTION()
	void OnRep_CurrentWeaponPresentationProfile();
protected:

	void ApplyLocomotionProfile();
	void LogAnimationProfileConfiguration() const;
	void UpdateMaxWalkSpeed();
	void ApplySprintAnimationState();
	UAnimMontage* GetEffectiveCombatIntroMontage() const;
	float GetEffectiveCombatIntroMontagePlayRate() const;
	UAnimMontage* GetEffectiveCombatOutroMontage() const;
	float GetEffectiveCombatOutroMontagePlayRate() const;
	bool ShouldPlayCombatIntroMontage() const;
	bool ShouldUseCombatRotationMode() const;
	bool ShouldInterruptCombatIntroOnHit() const;
	float GetMoveInputDeadZoneForAnimation() const;
	void UpdateMoveStartReplicationState(const FVector2D& MoveInput);
	void ResetMoveStartReplicationState();
	void DispatchMoveStartAnimationEvent(bool bWasSprintingForStart);
	void DispatchMoveStopAnimationEvent(bool bWasSprintingAtStop);
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

	/**
	 * Shared gameplay guard for combat-mode transitions. This is intentionally
	 * usable by the Gameplay Ability as well as local input, so prediction and
	 * server confirmation reject the same unsafe movement/presentation states.
	 */
	bool CanToggleCombatMode() const;

	/** Starts combat mode after the combat intro montage finishes. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void BeginCombatModeWithIntro();

	/** Stops the combat intro montage when a higher-priority reaction, such as hit react, needs to take over. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Animation")
	void InterruptCombatIntroForHit();

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleSkillInputTagPressed(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Combat|Input")
	void HandleSkillInputTagReleased(FGameplayTag InputTag);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSprint();

	/** Updates the held-sprint request from raw local movement input. */
	void UpdateSprintInputFromMove(const FVector2D& MoveInput);

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

	FORCEINLINE UProject_JWeaponPresentationComponent* GetWeaponPresentationComponent() const { return WeaponPresentationComponent; }
	FORCEINLINE UProject_JCombatHitValidationComponent* GetCombatHitValidationComponent() const { return CombatHitValidationComponent; }

	/** Returns LocomotionAnimStateComponent subobject **/
	FORCEINLINE class UProject_JLocomotionAnimStateComponent* GetLocomotionAnimStateComponent() const { return LocomotionAnimStateComponent; }

	/** Returns MotionMatchingTrajectoryComponent subobject **/
	FORCEINLINE class UProject_JMotionMatchingTrajectoryComponent* GetMotionMatchingTrajectoryComponent() const { return MotionMatchingTrajectoryComponent; }

	/**
	 * True only when the actor is expected to face its travel direction. Remote
	 * trajectory repair must not reinterpret intentional strafe/lock-on facing
	 * as a stale straight-running query.
	 */
	bool AllowsStraightRunningTrajectoryRepair() const;

	FORCEINLINE class UProject_JMountComponent* GetMountComponent() const { return MountComponent; }

	/**
	 * Persistent full-body animation context. Blueprint subclasses may extend
	 * this for swimming, vehicles, or transformations; brief actions belong in
	 * montages instead of changing this mode.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Animation|Locomotion")
	EProject_JAnimationLocomotionMode GetAnimationLocomotionMode() const;

	FORCEINLINE class AProject_JMountCharacter* GetSummonedMount() const { return SummonedMount; }

	class UProject_JInventoryComponent* GetInventoryComponent() const;

private:
	/** Cosmetic-only signal for remote clients; it never grants combat gameplay state. */
	UFUNCTION(Server, Reliable)
	void ServerSetCombatIntroPresentation(bool bShouldShowIntro);

	UFUNCTION(Server, Reliable)
	void ServerRequestUseMountItem(FGuid ItemInstanceId);

	UFUNCTION(Server, Reliable)
	void ServerTryInteract();

	/** Hard reference held only while the matching linked instance is active. */
	TSubclassOf<UAnimInstance> LinkedMountedAnimationLayerClass;

public:

	UFUNCTION(BlueprintPure, Category = "UI")
	UProject_JCharacterViewModel* GetCharacterViewModel() const;

	const UProject_JLocomotionProfile* GetLocomotionProfile() const;
	const UProject_JMotionMatchingAssetSet* GetMotionMatchingAssetSet() const;
	const UProject_JMotionMatchingAssetSet* GetCombatStrafeMotionMatchingAssetSet() const;
	const UProject_JWeaponAnimProfile* GetWeaponAnimProfile() const;
	const UProject_JCombatAnimProfile* GetCombatAnimProfile() const;
	const UProject_JCombatStyleDefinition* GetCombatStyleDefinition() const;

	UFUNCTION(BlueprintCallable, Category = "Combat|Style")
	void SetCurrentCombatStyle(UProject_JCombatStyleDefinition* InCombatStyle);

	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void SetCurrentWeaponPresentationProfile(UProject_JWeaponPresentationProfile* InPresentationProfile);

	UFUNCTION(BlueprintPure, Category = "Combat|Style")
	UProject_JCombatStyleDefinition* GetCurrentCombatStyle() const { return CurrentCombatStyle.Get(); }

	UFUNCTION(BlueprintPure, Category = "Combat|Weapon")
	UProject_JWeaponPresentationProfile* GetCurrentWeaponPresentationProfile() const { return CurrentWeaponPresentationProfile.Get(); }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsCombatModeActive() const;

	/** True while an entering-combat montage is preparing the combat animation layer. */
	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	bool IsCombatIntroPlaying() const { return bIsPlayingCombatIntro || bReplicatedCombatIntroPresentation; }

	/** True while the combat-exit sheathe montage owns the FullBody presentation. */
	UFUNCTION(BlueprintPure, Category = "Combat|Animation")
	bool IsCombatOutroPlaying() const { return bIsPlayingCombatOutro; }

	/** Called by the weapon's sheathe montage notify at the hand-to-back frame. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapon")
	void MoveWeaponToSheathedSocket();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAttacking() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDodging() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHitReacting() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprintLocomotionAllowed() const;

	/** True when the current local input may request a Sprint ability. */
	bool IsSprintInputDirectionAllowed() const;

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

	/** Derived locally from immutable equipped item data. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentCombatStyle, Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Style")
	TObjectPtr<UProject_JCombatStyleDefinition> CurrentCombatStyle = nullptr;

	/** Derived locally from immutable equipped item data. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeaponPresentationProfile, Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon")
	TObjectPtr<UProject_JWeaponPresentationProfile> CurrentWeaponPresentationProfile = nullptr;

	/** Cosmetic replication for remote AnimBPs and weapon presentation; gameplay remains ASC-authoritative. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedCombatModePresentation, Transient)
	bool bReplicatedCombatModePresentation = false;

	/** Replicates draw presentation immediately, without waiting for the post-montage combat Gameplay Effect. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedCombatIntroPresentation, Transient)
	bool bReplicatedCombatIntroPresentation = false;

	/** Tracks the early draw replay so the later authoritative combat state does not restart the montage. */
	bool bHasReceivedRemoteCombatIntroPresentation = false;

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

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveCombatOutroMontage = nullptr;

	bool bIsPlayingCombatOutro = false;

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
	bool bSprintInputHeld = false;
	FVector2D SprintMoveInput = FVector2D::ZeroVector;
	bool bAppliedCombatModeTag = false;
	bool bWasSprintLocomotionAllowed = false;
};
