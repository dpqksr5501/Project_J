#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Interaction/Project_JInteractable.h"
#include "Mount/Project_JMountTypes.h"
#include "Project_JMountCharacter.generated.h"

class ACharacter;

/**
 * Base class for controllable mounts. Create Blueprint children (for example
 * BP_Wyvern) and assign their mesh, animation blueprint, seat socket, and
 * movement tuning there.
 */
UCLASS(Abstract, Blueprintable)
class PROJECT_JMOUNT_API AProject_JMountCharacter : public ACharacter, public IAbilitySystemInterface, public IProject_JInteractable
{
	GENERATED_BODY()

public:
	AProject_JMountCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual bool CanInteract_Implementation(ACharacter* Interactor) const override;
	virtual void Interact_Implementation(ACharacter* Interactor) override;

	UFUNCTION(BlueprintPure, Category = "Mount")
	ACharacter* GetRider() const { return Rider; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsOccupied() const { return Rider != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	EProject_JMountState GetMountState() const { return MountState; }

	/** Resolves optional hand targets for the rider animation blueprint. */
	UFUNCTION(BlueprintPure, Category = "Mount|Rider IK")
	bool GetRiderHandIKTargetsWorld(FVector& OutLeftTarget, FVector& OutRightTarget) const;

	UFUNCTION(BlueprintPure, Category = "Mount|Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Mount|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount|Health")
	bool ApplyMountDamage(float Damage);

	/** Called by the rider's mount component on the authority. */
	bool TryMountRider(ACharacter* NewRider);

	/** Usable by forced-dismount gameplay such as death or despawn. Authority only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount")
	bool DismountRider(bool bForce = false);

	UFUNCTION(Server, Reliable)
	void ServerRequestDismount();

	UFUNCTION(BlueprintImplementableEvent, Category = "Mount")
	void K2_OnRiderMounted(ACharacter* NewRider);

	UFUNCTION(BlueprintImplementableEvent, Category = "Mount")
	void K2_OnRiderDismounted(ACharacter* PreviousRider);

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	UFUNCTION()
	void OnRep_Rider(ACharacter* PreviousRider);

	UFUNCTION()
	void OnRep_MountState(EProject_JMountState PreviousState);

	/** Socket on the mount mesh used to attach the hidden player avatar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	FName RiderSocketName = TEXT("RiderSocket");

	/** Optional mesh sockets for the rider's left/right hand FABRIK targets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider IK")
	FName RiderLeftHandSocketName = TEXT("Reins_L");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider IK")
	FName RiderRightHandSocketName = TEXT("Reins_R");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount|Rider IK")
	bool bEnableRiderHandIK = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount", meta = (ClampMin = "0.0"))
	float MountInteractionDistance = 300.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	bool bAllowAirDismount = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UProject_JAbilitySystemComponent> MountAbilitySystemComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UProject_JMountAttributeSet> MountAttributeSet = nullptr;

	/** Shared prompt/range/priority data for every concrete mount Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UProject_JInteractionTargetComponent> InteractionTargetComponent = nullptr;

	/** Possession camera shared by ground and flying mount Blueprints. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class USpringArmComponent> MountCameraBoom = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UCameraComponent> MountFollowCamera = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Rider, Transient)
	TObjectPtr<ACharacter> Rider = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_MountState, Transient)
	EProject_JMountState MountState = EProject_JMountState::Unmounted;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Mount|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 1000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Mount|Health")
	float Health = 1000.0f;

private:
	bool CanMountRider(const ACharacter* NewRider) const;
	bool FindDismountLocation(FVector& OutLocation) const;
	void AttachRider(ACharacter* NewRider) const;
	void DetachRider(ACharacter* PreviousRider, const FVector& DismountLocation) const;
	void NotifyRiderMountChanged(ACharacter* ChangedRider, bool bMounted);
};
