#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Mount/Project_JMountTypes.h"
#include "Project_JMountComponent.generated.h"

class AProject_JMountCharacter;
class UProject_JAbilitySystemComponent;
class UInputAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JMountChangedSignature, AProject_JMountCharacter*, PreviousMount, AProject_JMountCharacter*, NewMount);

/**
 * Player-avatar side of the mount relationship.
 *
 * The mount owns movement and possession while mounted; this component only
 * replicates which mount the player is riding and forwards the initial
 * client request to the server.
 */
UCLASS(ClassGroup = (Mount), meta = (BlueprintSpawnableComponent))
class PROJECT_JMOUNT_API UProject_JMountComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JMountComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Mount")
	bool RequestMount(AProject_JMountCharacter* Mount);

	UFUNCTION(BlueprintPure, Category = "Mount")
	AProject_JMountCharacter* GetMountedMount() const { return MountedMount; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsMounted() const { return MountedMount != nullptr; }

	/** Keep the rider's effective interaction action across the possession hand-off. */
	void SetRiderInteractAction(UInputAction* Action);
	const UInputAction* GetRiderInteractAction() const { return RiderInteractAction; }

	UPROPERTY(BlueprintAssignable, Category = "Mount")
	FProject_JMountChangedSignature OnMountChanged;

private:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	void ReleaseMountedTag();
	TWeakObjectPtr<UProject_JAbilitySystemComponent> MountedTagAbilitySystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RiderInteractAction = nullptr;
	UFUNCTION(Server, Reliable)
	void ServerRequestMount(AProject_JMountCharacter* Mount);

	UFUNCTION()
	void OnRep_MountedMount(AProject_JMountCharacter* PreviousMount);

	void SetMountedMount(AProject_JMountCharacter* NewMount);

	UPROPERTY(ReplicatedUsing = OnRep_MountedMount, Transient)
	TObjectPtr<AProject_JMountCharacter> MountedMount = nullptr;

	friend class AProject_JMountCharacter;
	friend class FProjectJMountedTagOwnershipTest;
};
