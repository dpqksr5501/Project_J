#pragma once
#include "CoreMinimal.h"
#include "TimerManager.h"
#include "UObject/Object.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Components/Project_JInventoryComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Project_JInventoryViewModel.generated.h"

class UProject_JInventoryComponent;
class UProject_JEquipmentManagerComponent;
class UProject_JInventoryViewModel;

/** Stable local list object; recycled widgets never become inventory owners. */
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JInventoryEntry : public UObject
{
	GENERATED_BODY()
  public:
	UPROPERTY(BlueprintReadOnly) FProject_JItemInstanceData Item;
	UPROPERTY(BlueprintReadOnly) EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
	UPROPERTY(BlueprintReadOnly) bool bEquipmentEntry = false;
	UPROPERTY() TWeakObjectPtr<UProject_JInventoryViewModel> Model;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProject_JInventoryPresentationChanged);

/** Event-driven owner projection and bounded request lifetime, independent of widget layout. */
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JInventoryViewModel : public UObject
{
	GENERATED_BODY()
  public:
	void Bind(UProject_JInventoryComponent *InInventory, UProject_JEquipmentManagerComponent *InEquipment);
	void Unbind();
	UFUNCTION(BlueprintCallable) bool Equip(FGuid InstanceId, EProject_JEquipmentSlot TargetSlot);
	UFUNCTION(BlueprintCallable) bool Unequip(FGuid InstanceId, EProject_JEquipmentSlot Slot);
	UFUNCTION(BlueprintCallable)
	bool CanDrop(const UProject_JInventoryEntry *Entry, EProject_JEquipmentSlot TargetSlot, bool bToInventory) const;
	UFUNCTION(BlueprintCallable) void Refresh();
	UFUNCTION(BlueprintCallable) bool Use(FGuid InstanceId);
	UFUNCTION(BlueprintCallable) void SetFilter(const FString &Query, int32 Kind, bool bSortByName);
	UFUNCTION(BlueprintPure) FText BuildTooltip(const UProject_JInventoryEntry *Entry) const;
	bool Split(const UProject_JInventoryEntry *Entry, int32 Count);
	bool DropInBag(const UProject_JInventoryEntry *Source, const UProject_JInventoryEntry *Target, bool bMerge);
	bool CanDropInBag(const UProject_JInventoryEntry *Source, const UProject_JInventoryEntry *Target, bool bMerge) const;
	DECLARE_MULTICAST_DELEGATE_OneParam(FSplitRequested, UProject_JInventoryEntry *);
	FSplitRequested OnSplitRequested;
	UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<UProject_JInventoryEntry>> VisibleEntries;
	UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<UProject_JInventoryEntry>> InventoryEntries;
	UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<UProject_JInventoryEntry>> EquipmentEntries;
	UPROPERTY(BlueprintReadOnly) bool bPending = false;
	UPROPERTY(BlueprintReadOnly) FText Status;
	UPROPERTY(BlueprintAssignable) FProject_JInventoryPresentationChanged OnChanged;
	virtual void BeginDestroy() override;

  private:
	bool SubmitBag(const FProject_JBagRequest &Request);
	UFUNCTION() void OnBagCompleted(FGuid RequestId, EProject_JBagResult Result);
	UFUNCTION() void OnUseCompleted(FGuid RequestId, EProject_JItemUseResult Result);
	void RebuildVisible();
	FString SearchQuery;
	int32 FilterKind = 0;
	bool bNameSort = false;
	bool Submit(FGuid InstanceId, EProject_JEquipmentSlot Slot, bool bUnequip);
	void RequestTimedOut();
	UFUNCTION() void OnItemChanged(const FProject_JItemInstanceData &Item);
	UFUNCTION() void OnEquipmentChanged(EProject_JEquipmentSlot Slot, class UProject_JEquipmentItemDefinition *Item);
	UFUNCTION() void OnCompleted(FGuid RequestId, const FProject_JEquipmentOperationResult &Result);
	TWeakObjectPtr<UProject_JInventoryComponent> Inventory;
	TWeakObjectPtr<UProject_JEquipmentManagerComponent> Equipment;
	FGuid PendingRequest;
	FTimerHandle RequestTimer;
	FTimerHandle RefreshTimer;
	void QueueRefresh();
};
