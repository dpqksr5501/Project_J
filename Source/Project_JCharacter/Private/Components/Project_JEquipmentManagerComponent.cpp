#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Net/UnrealNetwork.h"
#include "System/Project_JAssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"

// --- FFastArraySerializer Callbacks ---

void FProject_JEquipmentArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->OnRep_EquipmentAdded(Items[Index]);
		}
	}
}

void FProject_JEquipmentArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
}

void FProject_JEquipmentArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->OnRep_EquipmentRemoved(Items[Index]);
		}
	}
}

// --- UProject_JEquipmentManagerComponent ---

UProject_JEquipmentManagerComponent::UProject_JEquipmentManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JEquipmentManagerComponent, EquipmentArray);
}

void UProject_JEquipmentManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	EquipmentArray.OwnerComponent = this;
}

void UProject_JEquipmentManagerComponent::EquipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Check if already equipped
	for (const FProject_JEquipmentArrayItem& Item : EquipmentArray.Items)
	{
		if (Item.ItemDef == ItemDef)
		{
			return;
		}
	}

	// Add to replicated fast array
	FProject_JEquipmentArrayItem NewItem;
	NewItem.ItemDef = ItemDef;
	
	FProject_JEquipmentArrayItem& AddedItem = EquipmentArray.Items.Add_GetRef(NewItem);
	EquipmentArray.MarkItemDirty(AddedItem);

	// On Listen Server or Standalone, trigger local spawning manually since OnRep won't trigger for server owner
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		StartLocalSpawnEquipment(AddedItem);
	}

	// Grant GAS Abilities
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
	if (ASC)
	{
		for (TSubclassOf<UGameplayAbility> AbilityClass : ItemDef->GrantedAbilities)
		{
			if (AbilityClass)
			{
				FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, OwnerCharacter);
				FGameplayAbilitySpecHandle SpecHandle = ASC->GiveAbility(Spec);
				AddedItem.GrantedAbilityHandles.Add(SpecHandle);
			}
		}
	}
}

void UProject_JEquipmentManagerComponent::UnequipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < EquipmentArray.Items.Num(); ++i)
	{
		if (EquipmentArray.Items[i].ItemDef == ItemDef)
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex != INDEX_NONE)
	{
		ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
		if (OwnerCharacter && OwnerCharacter->GetNetMode() != NM_DedicatedServer)
		{
			OnRep_EquipmentRemoved(EquipmentArray.Items[FoundIndex]);
		}

		// Remove granted GAS abilities
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
		if (ASC)
		{
			for (const FGameplayAbilitySpecHandle& Handle : EquipmentArray.Items[FoundIndex].GrantedAbilityHandles)
			{
				if (Handle.IsValid())
				{
					ASC->ClearAbility(Handle);
				}
			}
		}

		EquipmentArray.Items.RemoveAt(FoundIndex);
		EquipmentArray.MarkArrayDirty();
	}
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item)
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		StartLocalSpawnEquipment(Item);
	}
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item)
{
	if (Item.SpawnedMesh)
	{
		Item.SpawnedMesh->DestroyComponent();
		Item.SpawnedMesh = nullptr;
	}
}

void UProject_JEquipmentManagerComponent::StartLocalSpawnEquipment(FProject_JEquipmentArrayItem& Item)
{
	UProject_JEquipmentItemDefinition* ItemDef = Item.ItemDef;
	if (!ItemDef || ItemDef->EquipmentMesh.IsNull())
	{
		return;
	}

	// Request ASYNC Loading of modular mesh to prevent game-thread hitches
	TWeakObjectPtr<UProject_JEquipmentManagerComponent> WeakThis(this);
	UProject_JAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
		ItemDef->EquipmentMesh.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda([WeakThis, ItemDef]()
		{
			if (UProject_JEquipmentManagerComponent* StrongThis = WeakThis.Get())
			{
				StrongThis->OnEquipmentMeshLoaded(ItemDef);
			}
		})
	);
}

void UProject_JEquipmentManagerComponent::OnEquipmentMeshLoaded(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef) return;

	// Verify the item is still equipped (handling race conditions where gear is unequipped while loading)
	FProject_JEquipmentArrayItem* FoundItem = nullptr;
	for (FProject_JEquipmentArrayItem& Item : EquipmentArray.Items)
	{
		if (Item.ItemDef == ItemDef)
		{
			FoundItem = &Item;
			break;
		}
	}

	if (!FoundItem || FoundItem->SpawnedMesh)
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	USkeletalMeshComponent* MainMesh = OwnerCharacter->GetMesh();
	if (!MainMesh) return;

	// Spawn the modular mesh component dynamically on the client
	UProject_JModularMeshComponent* NewMeshComp = NewObject<UProject_JModularMeshComponent>(OwnerCharacter);
	NewMeshComp->RegisterComponent();
	NewMeshComp->SetSkeletalMesh(ItemDef->EquipmentMesh.Get());

	// Attach to specific socket or standard Leader Pose
	if (ItemDef->AttachSocketName.IsNone())
	{
		// Standard armor/body part: Follow leader pose
		NewMeshComp->AttachAndSetLeader(MainMesh);
	}
	else
	{
		// Weapon or accessory: Attach to socket and disable leader pose (usually rigid)
		NewMeshComp->AttachToComponent(MainMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ItemDef->AttachSocketName);
		NewMeshComp->SetLeaderPoseComponent(nullptr); 
	}

	FoundItem->SpawnedMesh = NewMeshComp;

	// Inject Weapon Anim Profile to local player if applicable
	if (ItemDef->WeaponAnimProfile && OwnerCharacter->IsLocallyControlled())
	{
		// Cast to AProject_JPlayerCharacter and apply weapon anim profile
	}
}
