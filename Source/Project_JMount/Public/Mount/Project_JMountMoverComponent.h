#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "Interaction/Project_JInteractable.h"
#include "Mount/Project_JMountMoverTypes.h"
#include "MoverSimulationTypes.h"
#include "Movement/Project_JMoverComponent.h"
#include "World/Project_JStreamingMovementSource.h"
#include "Project_JMountMoverComponent.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UProject_JAbilitySystemComponent;
class UProject_JMountAttributeSet;
class UProject_JInteractionTargetComponent;
class UProject_JRiderAnimationProfile;
struct FOnAttributeChangeData;

/** Local travel comfort input sampled before Mover records the command. */
UINTERFACE(MinimalAPI)
class UProject_JFlightStreamingInputSource : public UInterface
{
	GENERATED_BODY()
};

class PROJECT_JMOUNT_API IProject_JFlightStreamingInputSource
{
	GENERATED_BODY()
public:
	virtual float GetLocalFlightStreamingSpeedScale(const APawn& Mount) const = 0;
};

/** Mover mode for ground mounts with speed-dependent turn radius and bank output (BP03). */
UCLASS()
class PROJECT_JMOUNT_API UProject_JGroundMountMode : public UWalkingMode
{
	GENERATED_BODY()
public:
	explicit UProject_JGroundMountMode(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FProject_JMountMotionProfile Profile;

	virtual void GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& Start, const FMoverTimeStep& Step, FProposedMove& Move) const override;
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output) override;
};

/** Mover mode for 3D energy-flying mounts with dive acceleration, stall protection, and glide recovery (BP03). */
UCLASS()
class PROJECT_JMOUNT_API UProject_JFlyingMountMode : public UBaseMovementMode
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FProject_JMountMotionProfile Profile;

	virtual void GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& Start, const FMoverTimeStep& Step, FProposedMove& Move) const override;
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& Output) override;
};

/**
 * Unified Mover component for ground and flying mounts (BP03).
 * Runs on the same UProject_JMoverComponent / Network Prediction pipeline as AProject_JPlayerCharacter.
 */
UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class PROJECT_JMOUNT_API UProject_JMountMoverComponent : public UProject_JMoverComponent
{
	GENERATED_BODY()

public:
	UProject_JMountMoverComponent();
	virtual void OnRegister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	FProject_JMountMotionProfile MotionProfile;

	static const FName GroundMountModeName;
	static const FName FlyingMountModeName;

	/** Enters FlyingMountMode when bCanFly is enabled and vertical clearance check succeeds. */
	UFUNCTION(BlueprintCallable, Category = "Mount|Mover")
	bool TryEnterFlightMode();
	bool HasTakeoffClearance(const FVector& Location) const;

	/** Force-stops flight on fatal damage or unpossess (exits Flying -> Falling so mounts never hover forever). */
	UFUNCTION(BlueprintCallable, Category = "Mount|Mover")
	void ForceStopFlightToFalling();

	/** Reads the latest committed mount sync state (flight state, air speed, pitch, bank, stamina). */
	FProject_JMountMoverSyncState GetLatestMountSyncState() const;

	/** Capsule-channel landing surface probe (ignores ECC_Pawn and non-blocking water triggers). */
	bool FindSafeLandingSurface(FHitResult& OutHit, float ProbeDistanceCm = 600.0f) const;
};

/**
 * Next-generation Mover-native Mount Pawn with full production parity (BP03):
 * GAS health/death depletion, IProject_JInteractable mount/dismount, rider attachment/restoration,
 * reins Hand IK targets, and force-stop flight safety on unpossess/death.
 */
UCLASS(Blueprintable)
class PROJECT_JMOUNT_API AProject_JMountMoverPawn : public APawn, public IAbilitySystemInterface, public IProject_JInteractable, public IMoverInputProducerInterface, public IProject_JStreamingMovementSource
{
	GENERATED_BODY()

public:
	AProject_JMountMoverPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void OnRep_Controller() override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmd) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount")
	bool TryMountRider(APawn* NewRider);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount")
	bool DismountRider(bool bForce = false);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Mount")
	void ServerRequestDismount();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Mount")
	void ServerRequestToggleFlight();

	/** Allows PlayerController wheel input to smoothly zoom the mount boom. */
	UFUNCTION(BlueprintCallable, Category = "Mount|Camera")
	bool ApplyWheelZoom(float WheelDelta);

	/** Programmatic / test input injection into the next ProduceInput simulation step. */
	UFUNCTION(BlueprintCallable, Category = "Mount|Input")
	void SetMountControlInput(FVector2D InThrottleTurn, float InPitchInput = 0.0f, bool bInBoost = false, bool bInGlide = false);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount|Health")
	bool ApplyMountDamage(float Damage);

	UFUNCTION(BlueprintPure, Category = "Mount|Rider IK")
	bool GetRiderHandIKTargets(FVector& OutLeftTarget, FVector& OutRightTarget) const;

	UFUNCTION(BlueprintPure, Category = "Mount|Rider")
	const UProject_JRiderAnimationProfile* GetRiderAnimationProfile() const { return RiderAnimationProfile; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	APawn* GetRider() const { return Rider; }

	APlayerState* ResolveRiderAbilityOwner(const APawn* ExpectedRider) const;

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsMounted() const { return Rider != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	UProject_JMountMoverComponent* GetMountMover() const { return MountMover; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	UCapsuleComponent* GetCapsuleComponent() const { return Capsule; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	USkeletalMeshComponent* GetMesh() const { return MountMesh; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsFlying() const;
	virtual bool UsesAirborneStreamingPrediction_Implementation() const override { return IsFlying(); }

	UFUNCTION(BlueprintPure, Category = "Mount|Health")
	float GetHealth() const { return Health; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	FName RiderSocketName = TEXT("RiderSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider IK")
	FName RiderLeftHandSocketName = TEXT("Reins_L");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider IK")
	FName RiderRightHandSocketName = TEXT("Reins_R");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount", meta = (ClampMin = "50.0"))
	float MountInteractionDistance = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider")
	TObjectPtr<UProject_JRiderAnimationProfile> RiderAnimationProfile;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputMappingContext> MountMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> PitchOrAscendAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> GlideAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> TakeOffAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> LandAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Input")
	TObjectPtr<UInputAction> DismountAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Camera", meta = (ClampMin = "150.0", ClampMax = "1500.0"))
	float GroundCameraArmLength = 520.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Camera", meta = (ClampMin = "200.0", ClampMax = "2000.0"))
	float FlyingCameraArmLength = 660.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Camera", meta = (ClampMin = "60.0", ClampMax = "125.0"))
	float BaseCameraFOV = 94.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Camera", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float DiveFOVBoost = 14.0f;

protected:
	void OnHealthAttributeChanged(const FOnAttributeChangeData& Data);
	void HandleMoveInput(const FInputActionValue& Value);
	void HandleMoveCompleted(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandlePitchInput(const FInputActionValue& Value);
	void HandlePitchCompleted(const FInputActionValue& Value);
	void HandleBoostStarted(const FInputActionValue& Value);
	void HandleBoostCompleted(const FInputActionValue& Value);
	void HandleGlideStarted(const FInputActionValue& Value);
	void HandleGlideCompleted(const FInputActionValue& Value);
	void HandleTakeOffTriggered(const FInputActionValue& Value);
	void HandleLandTriggered(const FInputActionValue& Value);
	void HandleDismountTriggered(const FInputActionValue& Value);
	void ApplyMountMappingContext(bool bAdd);
	void GrantRiderIntegrityWhitelist(APawn* TargetRider) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount")
	TObjectPtr<USkeletalMeshComponent> MountMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount")
	TObjectPtr<UProject_JMountMoverComponent> MountMover;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Abilities")
	TObjectPtr<UProject_JAbilitySystemComponent> MountAbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Abilities")
	TObjectPtr<UProject_JMountAttributeSet> MountAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UProject_JInteractionTargetComponent> InteractionTargetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(Replicated, Transient)
	TObjectPtr<APawn> Rider = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Health")
	float Health = 1000.0f;

private:
	bool DismountRiderInternal(bool bForce, bool bRestorePossession);
	FDelegateHandle HealthChangedHandle;
	TWeakObjectPtr<AController> SessionController;
	TWeakObjectPtr<UProject_JAbilitySystemComponent> MountedTagAbilitySystem;
	TWeakObjectPtr<class ULocalPlayer> BoundInputLocalPlayer;
	ECollisionEnabled::Type SavedRiderCollision = ECollisionEnabled::QueryAndPhysics;
	EMovementMode SavedRiderMode = MOVE_Walking;
	uint8 SavedRiderCustomMode = 0;
	bool bHasSavedRiderState = false;
	bool bDismountInProgress = false;

	FVector2D CachedThrottleTurn = FVector2D::ZeroVector;
	float CachedPitchInput = 0.0f;
	bool bCachedBoostHeld = false;
	bool bCachedGlideHeld = false;
	bool bPendingTakeOff = false;
	bool bPendingLand = false;
	uint8 CachedFlightActionSeq = 0;
	float WheelZoomOffset = 0.0f;
};
