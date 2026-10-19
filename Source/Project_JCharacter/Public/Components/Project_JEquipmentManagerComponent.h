#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "Project_JEquipmentManagerComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JEquipmentManagerComponent;
class UProject_JInventoryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JEquipmentChangedSignature, EProject_JEquipmentSlot, Slot,
											 UProject_JEquipmentItemDefinition *, ItemDef);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JEquipmentRequestCompleted, FGuid, RequestId,
											 const FProject_JEquipmentOperationResult &, Result);

USTRUCT(BlueprintType)
struct FProject_JEquipmentArrayItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	UProject_JEquipmentItemDefinition *ItemDef = nullptr;

	UPROPERTY()
	FProject_JItemInstanceData ItemInstance;

	UPROPERTY()
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
};

USTRUCT(BlueprintType)
struct FProject_JEquipmentArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FProject_JEquipmentArrayItem> Items;

	UPROPERTY(NotReplicated)
	UProject_JEquipmentManagerComponent *OwnerComponent = nullptr;

	void PostReplicatedAdd(const TArrayView<int32> &AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> &ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32> &RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo &DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FProject_JEquipmentArrayItem, FProject_JEquipmentArray>(
			Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FProject_JEquipmentArray> : public TStructOpsTypeTraitsBase2<FProject_JEquipmentArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

/** Read-only observer projection. Private instance IDs, counts, locks and bag order never enter it. */
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JPublicEquipmentItem : public FFastArraySerializerItem
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UProject_JEquipmentItemDefinition> ItemDef = nullptr;
	UPROPERTY() EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
};

USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JPublicEquipmentArray : public FFastArraySerializer
{
	GENERATED_BODY()
	UPROPERTY() TArray<FProject_JPublicEquipmentItem> Items;
	UPROPERTY(NotReplicated) TObjectPtr<UProject_JEquipmentManagerComponent> OwnerComponent = nullptr;
	void PostReplicatedAdd(const TArrayView<int32> &Indices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> &Indices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32> &Indices, int32 FinalSize);
	bool NetDeltaSerialize(FNetDeltaSerializeInfo &Params)
	{
		return FastArrayDeltaSerialize<FProject_JPublicEquipmentItem, FProject_JPublicEquipmentArray>(Items, Params,
																									  *this);
	}
};

template <>
struct TStructOpsTypeTraits<FProject_JPublicEquipmentArray> : TStructOpsTypeTraitsBase2<FProject_JPublicEquipmentArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

/**
 * Manages replicated equipment data on the PlayerState.
 * The visual representation is handled by EquipmentRuntimeComponent on the Character.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JEquipmentManagerComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	UProject_JEquipmentManagerComponent();
	FProject_JPersistentSubmit SubmitPersistentEdit;
	TFunction<FGuid()> CreatePersistentRequest;
	/** Keep the returned ID unchanged until completion. Old sessions expire. */
	UFUNCTION(BlueprintCallable, Category = "Requests")
	FGuid NewRequestId() const
	{
		return CreatePersistentRequest ? CreatePersistentRequest() : FGuid::NewGuid();
	}
	TFunction<bool()> PersistentAdmission;
	/** Provider lifetime ended; consumers release their own grants/visual leases. */
	FSimpleMulticastDelegate OnRetired;
	bool IsAcceptingRequests() const
	{
		return CanPublishState();
	}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FProject_JEquipmentRequestCompleted OnRequestCompleted;

	/** Correlated owner-only replies. UI never supplies item definitions or predicted equipment state. */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipmentChange(FGuid RequestId, FGuid InstanceId, EProject_JEquipmentSlot Slot, bool bUnequip);
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool GetEquippedItemInstance(EProject_JEquipmentSlot Slot, FProject_JItemInstanceData &OutItem) const;
	/** Actual locally received private slot records; used to verify observer privacy. */
	int32 GetPrivateEquipmentCount() const
	{
		return EquipmentArray.Items.Num();
	}
	TArray<FProject_JEquipmentArrayItem> CapturePersistentEquipment() const
	{
		return EquipmentArray.Items;
	}
	bool ValidatePersistentEquipment(const TArray<FProject_JEquipmentArrayItem> &Slots,
									 const TArray<FProject_JItemInstanceData> &Inventory,
									 bool bTrustedApply = false) const;
	void InstallPersistentEquipment(const TArray<FProject_JEquipmentArrayItem> &Slots);
	void PublishPersistentEquipment(const TArray<FProject_JEquipmentArrayItem> &Before);
	/** Restored unequipped state must survive respawn without prototype reseeding. */
	bool bPersistentProfileBound = false;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FProject_JEquipmentChangedSignature OnEquipmentEquipped;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FProject_JEquipmentChangedSignature OnEquipmentUnequipped;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItem(UProject_JEquipmentItemDefinition *ItemDef);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItemInstance(const FProject_JItemInstanceData &ItemInstance);

	/** Preferred network request contract: the server resolves authoritative item data by id. */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipItemInstanceById(FGuid InstanceId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	FProject_JEquipmentOperationResult TryEquipItemInstanceById(FGuid InstanceId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipItem(UProject_JEquipmentItemDefinition *ItemDef);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipSlot(EProject_JEquipmentSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestUnequipSlot(EProject_JEquipmentSlot Slot);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UProject_JEquipmentItemDefinition *GetEquippedItemInSlot(EProject_JEquipmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	TArray<UProject_JEquipmentItemDefinition *> GetAllEquippedItems() const;

	/** Development diagnostics: compact state identity for verifying FastArray replication across PIE worlds. */
	FString GetReplicationDiagnosticSummary() const;

	/** Development diagnostics: client-side FastArray delta callbacks observed since the last reset. */
	FString GetReplicationDiagnosticDeltaSummary() const;
	void ResetReplicationDiagnosticDeltaCounters();

	void OnRep_EquipmentAdded(FProject_JEquipmentArrayItem &Item);
	void OnRep_EquipmentChanged(FProject_JEquipmentArrayItem &Item);
	void OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem &Item);
	void HandlePublicEquipment(const FProject_JPublicEquipmentItem &Item, bool bRemoved);

  protected:
	virtual void BeginPlay() override;

  private:
	friend class FProjectJAuditEquipmentNetworkTest;
	friend class FProjectJEquipmentPrivacyTest;
	bool bEnding = false;
	bool CanPublishState() const;
	bool HasMutationAuthority() const;
	void Retire();
	UFUNCTION(Server, Reliable)
	void ServerRequestEquipmentChange(FGuid RequestId, FGuid InstanceId, EProject_JEquipmentSlot Slot, bool bUnequip);
	UFUNCTION(Client, Reliable)
	void ClientEquipmentRequestCompleted(FGuid RequestId, FProject_JEquipmentOperationResult Result);
	FProject_JEquipmentOperationResult ExecuteEquipmentRequest(FGuid InstanceId, EProject_JEquipmentSlot Slot,
															   bool bUnequip, FGuid RequestId = FGuid());
	FProject_JEquipmentOperationResult SubmitPersistentEquipment(FGuid RequestId, FGuid InstanceId,
																 EProject_JEquipmentSlot Slot, bool bUnequip);
	double RequestWindowStart = -1.0;
	int32 RequestsInWindow = 0;
	UFUNCTION(Server, Reliable)
	void ServerRequestEquipItemInstanceById(FGuid InstanceId);

	UFUNCTION(Server, Reliable)
	void ServerRequestUnequipSlot(EProject_JEquipmentSlot Slot);

	bool CanCommitEquipItemInstance(const FProject_JItemInstanceData &ItemInstance) const;
	EProject_JEquipmentOperationFailure ValidateInventoryItemInstance(
		const FProject_JItemInstanceData &ItemInstance) const;
	UProject_JInventoryComponent *GetOwnerInventoryComponent() const;
	bool SetInventoryEquipmentLock(const FProject_JItemInstanceData &ItemInstance, bool bLocked) const;
	FProject_JEquipmentOperationResult CommitEquipItemInstance(const FProject_JItemInstanceData &ItemInstance,
															   bool bRequireInventoryOwnership);
	void BroadcastEquipmentEquipped(const FProject_JEquipmentArrayItem &Item);
	void BroadcastEquipmentUnequipped(const FProject_JEquipmentArrayItem &Item);
	int32 FindEquipmentIndexByItem(const UProject_JEquipmentItemDefinition *ItemDef) const;
	int32 FindEquipmentIndexByInstanceId(const FGuid &InstanceId) const;
	int32 FindEquipmentIndexBySlot(EProject_JEquipmentSlot Slot) const;
	bool RemoveEquipmentAt(int32 Index);
	bool UsesPublicEquipment() const;

  private:
	// Prevent delegate callbacks from nesting slot replacement/teardown. No worker lock.
	bool bEquipmentOperationInProgress = false;

	UPROPERTY(Replicated)
	FProject_JEquipmentArray EquipmentArray;
	/** Derived only by authoritative equipment commits, before publication. */
	UPROPERTY(Replicated)
	FProject_JPublicEquipmentArray PublicEquipmentArray;

#if !UE_BUILD_SHIPPING
	int32 ReplicationDiagnosticAddedCount = 0;
	int32 ReplicationDiagnosticChangedCount = 0;
	int32 ReplicationDiagnosticRemovedCount = 0;
#endif
};
