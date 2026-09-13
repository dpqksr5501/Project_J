#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "Interaction/Project_JInteractable.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Project_JInternalTestReceiver.generated.h"

// Transient native fixture. No project assets or Blueprint modifications are needed.
UCLASS(Transient)
class UProject_JInternalTestReceiver : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UProject_JInventoryComponent> Inventory;
	UPROPERTY() TObjectPtr<UProject_JEquipmentManagerComponent> Equipment;
	bool bMutate = true;
	bool bPayloadStable = false;
	bool bRemovalCommitted = false;
	bool bRecursiveRemovalRejected = false;
	FGuid OtherItem;
	FGuid ReentrantEquipmentItem;
	EProject_JEquipmentOperationFailure ReentrantFailure = EProject_JEquipmentOperationFailure::None;

	UFUNCTION() void OnAdded(const FProject_JItemInstanceData& Item)
	{
		if (!bMutate) return;
		bMutate = false;
		const FGuid OriginalId = Item.InstanceId;
		UProject_JItemDefinition* Definition = Item.ItemDef;
		for (int32 Index = 0; Index < 128; ++Index) Inventory->AddItemDefinition(Definition);
		bPayloadStable = Item.InstanceId == OriginalId;
	}
	UFUNCTION() void OnRemoved(const FProject_JItemInstanceData& Item)
	{
		if (!bMutate) return;
		bMutate = false;
		bRemovalCommitted = !Inventory->HasItemInstance(Item.InstanceId);
		bRecursiveRemovalRejected = !Inventory->RemoveItemInstance(Item.InstanceId);
		Inventory->RemoveItemInstance(OtherItem);
	}
	UFUNCTION() void OnUnequipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* Definition)
	{
		bRemovalCommitted = Equipment->GetEquippedItemInSlot(Slot) == nullptr;
		ReentrantFailure = Equipment->TryEquipItemInstanceById(ReentrantEquipmentItem).Failure;
	}
};

UCLASS(Transient)
class AProject_JInternalInteractionTarget : public AActor, public IProject_JInteractable
{
	GENERATED_BODY()
public:
	bool bAllowed = true;
	int32 Interactions = 0;
	virtual bool CanInteract_Implementation(ACharacter* Interactor) const override { return bAllowed; }
	virtual void Interact_Implementation(ACharacter* Interactor) override { ++Interactions; }
};

UCLASS(Transient)
class UProject_JInternalInputAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override {}
};
