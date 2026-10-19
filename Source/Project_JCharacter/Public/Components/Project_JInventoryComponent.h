#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "Project_JInventoryComponent.generated.h"

class UProject_JItemDefinition;
class UProject_JEquipmentItemDefinition;
class UProject_JInventoryComponent;

UENUM(BlueprintType)
enum class EProject_JItemUseResult : uint8
{
	Success,
	Invalid,
	Locked,
	Cooldown,
	NoEffect,
	Unavailable,
	RateLimited,
	Pending,
	Reconciled
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JItemUseCompleted, FGuid, RequestId, EProject_JItemUseResult,
											 Result);

UENUM(BlueprintType)
enum class EProject_JBagOperation : uint8
{
	Split,
	Merge,
	Swap
};
UENUM(BlueprintType)
enum class EProject_JBagResult : uint8
{
	Success,
	Invalid,
	Locked,
	Changed,
	Incompatible,
	Full,
	Unavailable,
	RateLimited,
	Pending
};
USTRUCT(BlueprintType)
struct FProject_JBagRequest
{
	GENERATED_BODY()
	UPROPERTY() EProject_JBagOperation Operation = EProject_JBagOperation::Split;
	UPROPERTY() FGuid Source;
	UPROPERTY() FGuid Target;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() int32 ExpectedSourceCount = 0;
	UPROPERTY() int32 ExpectedTargetCount = 0;
	UPROPERTY() int32 ExpectedSourceOrder = INDEX_NONE;
	UPROPERTY() int32 ExpectedTargetOrder = INDEX_NONE;
	bool operator==(const FProject_JBagRequest &Other) const
	{
		return Operation == Other.Operation && Source == Other.Source && Target == Other.Target &&
			   Count == Other.Count && ExpectedSourceCount == Other.ExpectedSourceCount &&
			   ExpectedTargetCount == Other.ExpectedTargetCount && ExpectedSourceOrder == Other.ExpectedSourceOrder &&
			   ExpectedTargetOrder == Other.ExpectedTargetOrder;
	}
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JBagCompleted, FGuid, RequestId, EProject_JBagResult, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProject_JInventoryItemChangedSignature, const FProject_JItemInstanceData &,
											ItemInstance);

USTRUCT(BlueprintType)
struct FProject_JInventoryArrayItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FProject_JItemInstanceData ItemInstance;
};

USTRUCT(BlueprintType)
struct FProject_JInventoryArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FProject_JInventoryArrayItem> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<UProject_JInventoryComponent> OwnerComponent = nullptr;

	void PostReplicatedAdd(const TArrayView<int32> &AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> &ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32> &RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo &DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FProject_JInventoryArrayItem, FProject_JInventoryArray>(
			Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FProject_JInventoryArray> : public TStructOpsTypeTraitsBase2<FProject_JInventoryArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

UCLASS(ClassGroup = (Inventory), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	UProject_JInventoryComponent();
	FProject_JPersistentSubmit SubmitPersistentEdit;
	TFunction<FGuid()> CreatePersistentRequest;
	/** Keep the returned ID unchanged until completion. Old sessions expire. */
	UFUNCTION(BlueprintCallable, Category = "Requests")
	FGuid NewRequestId() const
	{
		return CreatePersistentRequest ? CreatePersistentRequest() : FGuid::NewGuid();
	}
	TFunction<bool()> PersistentAdmission;
	bool IsAcceptingRequests() const
	{
		return CanPublishState();
	}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	UFUNCTION(BlueprintCallable) void RequestUseItem(FGuid RequestId, FGuid InstanceId);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly) EProject_JItemUseResult TryUseItem(FGuid InstanceId);
	UPROPERTY(BlueprintAssignable) FProject_JItemUseCompleted OnUseCompleted;
	UFUNCTION(BlueprintPure) float GetUseCooldownRemaining() const;
	void RequestBagChange(FGuid RequestId, const FProject_JBagRequest &Request);
	/** Atomic authoritative edit. Callers provide observed counts/order to reject stale drags. */
	EProject_JBagResult TryBagChange(const FProject_JBagRequest &Request);
	UPROPERTY(BlueprintAssignable) FProject_JBagCompleted OnBagCompleted;

	/** Read-only value snapshot for owner UI; no second inventory state is maintained. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	TArray<FProject_JItemInstanceData> GetItemInstances() const;
	/** Trusted persistence adapter: validate the whole batch before any replacement. */
	bool ValidatePersistentItems(const TArray<FProject_JItemInstanceData> &Items, bool bTrustedApply = false) const;
	void InstallPersistentItems(const TArray<FProject_JItemInstanceData> &Items);
	void PublishPersistentItems(const TArray<FProject_JItemInstanceData> &Before);
	bool CanUsePersistentItem(FGuid Instance) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemAdded;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemRemoved;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	FProject_JItemInstanceData AddItemDefinition(UProject_JItemDefinition *ItemDef, int32 StackCount = 1,
												 int32 ItemLevel = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool RemoveItemInstance(FGuid InstanceId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool SetItemStackCount(FGuid InstanceId, int32 NewStackCount);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool AddItemStackCount(FGuid InstanceId, int32 DeltaStackCount);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool ConsumeItemStack(FGuid InstanceId, int32 CountToConsume = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool SetItemInstanceLocked(FGuid InstanceId, bool bLocked, bool bEquipped = false);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItemInstance(FGuid InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsItemInstanceLocked(FGuid InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanRemoveItemInstance(FGuid InstanceId, int32 Count = 1) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanMoveItemInstance(FGuid InstanceId, int32 Count = 1) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool FindItemInstance(FGuid InstanceId, FProject_JItemInstanceData &OutItemInstance) const;

	/** Development diagnostics: compact state identity for verifying FastArray replication across PIE worlds. */
	FString GetReplicationDiagnosticSummary() const;

	/** Development diagnostics: client-side FastArray delta callbacks observed since the last reset. */
	FString GetReplicationDiagnosticDeltaSummary() const;
	void ResetReplicationDiagnosticDeltaCounters();

	void HandleReplicatedItemAdded(const FProject_JInventoryArrayItem &Item);
	void HandleReplicatedItemChanged(const FProject_JInventoryArrayItem &Item);
	void HandleReplicatedItemRemoved(const FProject_JInventoryArrayItem &Item);

  protected:
	virtual void BeginPlay() override;

  private:
	friend class FProjectJCommandReplayTest;
	bool bEnding = false;
	EProject_JBagResult SubmitPersistentBag(FGuid RequestId, const FProject_JBagRequest &Request);
	EProject_JItemUseResult SubmitPersistentUse(FGuid RequestId, FGuid InstanceId);
	bool CanPublishState() const;
	bool HasMutationAuthority() const;
	void Retire();
	UFUNCTION(Server, Reliable) void ServerBagChange(FGuid RequestId, FProject_JBagRequest Request);
	UFUNCTION(Client, Reliable) void ClientBagCompleted(FGuid RequestId, EProject_JBagResult Result);
	struct FBagReceipt
	{
		FProject_JBagRequest Request;
		EProject_JBagResult Result;
	};
	TMap<FGuid, FBagReceipt> BagReceipts;
	TArray<FGuid> BagReceiptOrder;
	double BagRequestWindow = -1;
	int32 BagRequestCount = 0;
	bool bEditingBag = false;
	int32 NextBagOrder() const;
	UFUNCTION(Server, Reliable) void ServerUseItem(FGuid RequestId, FGuid InstanceId);
	UFUNCTION(Client, Reliable) void ClientUseCompleted(FGuid RequestId, EProject_JItemUseResult Result);
	UFUNCTION() void OnRep_NextUseTime();
	UPROPERTY(ReplicatedUsing = OnRep_NextUseTime) double NextUseTime = 0;
	struct FUseReceipt
	{
		FGuid InstanceId;
		EProject_JItemUseResult Result;
	};
	TMap<FGuid, FUseReceipt> UseReceipts;
	TArray<FGuid> UseReceiptOrder;
	double UseRequestWindow = -1;
	int32 UseRequestCount = 0;
	bool bUsingItem = false;
	bool CanCommitItemInstance(const FProject_JItemInstanceData &ItemInstance) const;
	int32 FindItemIndex(FGuid InstanceId) const;

	UPROPERTY(Replicated)
	FProject_JInventoryArray InventoryArray;

#if !UE_BUILD_SHIPPING
	int32 ReplicationDiagnosticAddedCount = 0;
	int32 ReplicationDiagnosticChangedCount = 0;
	int32 ReplicationDiagnosticRemovedCount = 0;
#endif
};
