#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAttributeSet.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JStatTypes.h"
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
	RefreshCurrentWeaponAnimProfile();
}

void UProject_JEquipmentManagerComponent::EquipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	FProject_JItemInstanceData ItemInstance;
	ItemInstance.InstanceId = FGuid::NewGuid();
	ItemInstance.ItemDef = ItemDef;
	EquipItemInstance(ItemInstance);
}

void UProject_JEquipmentManagerComponent::EquipItemInstance(const FProject_JItemInstanceData& ItemInstance)
{
	UProject_JEquipmentItemDefinition* ItemDef = ItemInstance.ItemDef;
	if (!ItemDef || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (FindEquipmentIndexByItem(ItemDef) != INDEX_NONE)
	{
		return;
	}

	const EProject_JEquipmentSlot Slot = ItemDef->EquipmentSlot;
	if (Slot != EProject_JEquipmentSlot::None)
	{
		const int32 ExistingSlotIndex = FindEquipmentIndexBySlot(Slot);
		if (ExistingSlotIndex != INDEX_NONE)
		{
			RemoveEquipmentAt(ExistingSlotIndex);
		}
	}

	// Add to replicated fast array
	FProject_JEquipmentArrayItem NewItem;
	NewItem.ItemDef = ItemDef;
	NewItem.ItemInstance = ItemInstance;
	if (!NewItem.ItemInstance.InstanceId.IsValid())
	{
		NewItem.ItemInstance.InstanceId = FGuid::NewGuid();
	}
	NewItem.Slot = Slot;
	
	FProject_JEquipmentArrayItem& AddedItem = EquipmentArray.Items.Add_GetRef(NewItem);
	EquipmentArray.MarkItemDirty(AddedItem);
	BroadcastEquipmentEquipped(AddedItem);
	RefreshCurrentWeaponAnimProfile();

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

	ApplyEquipmentStatModifiers(ItemDef, 1.0f);
}

void UProject_JEquipmentManagerComponent::UnequipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 FoundIndex = FindEquipmentIndexByItem(ItemDef);
	if (FoundIndex != INDEX_NONE)
	{
		RemoveEquipmentAt(FoundIndex);
	}
}

void UProject_JEquipmentManagerComponent::UnequipSlot(EProject_JEquipmentSlot Slot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 FoundIndex = FindEquipmentIndexBySlot(Slot);
	if (FoundIndex != INDEX_NONE)
	{
		RemoveEquipmentAt(FoundIndex);
	}
}

UProject_JEquipmentItemDefinition* UProject_JEquipmentManagerComponent::GetEquippedItemInSlot(EProject_JEquipmentSlot Slot) const
{
	const int32 FoundIndex = FindEquipmentIndexBySlot(Slot);
	return FoundIndex != INDEX_NONE ? EquipmentArray.Items[FoundIndex].ItemDef : nullptr;
}

void UProject_JEquipmentManagerComponent::RemoveEquipmentAt(int32 Index)
{
	if (!EquipmentArray.Items.IsValidIndex(Index))
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		OnRep_EquipmentRemoved(EquipmentArray.Items[Index]);
	}
	else
	{
		BroadcastEquipmentUnequipped(EquipmentArray.Items[Index]);
	}

	// Remove granted GAS abilities
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerCharacter);
	if (ASC)
	{
		for (const FGameplayAbilitySpecHandle& Handle : EquipmentArray.Items[Index].GrantedAbilityHandles)
		{
			if (Handle.IsValid())
			{
				ASC->ClearAbility(Handle);
			}
		}
	}

	ApplyEquipmentStatModifiers(EquipmentArray.Items[Index].ItemDef, -1.0f);

	EquipmentArray.Items.RemoveAt(Index);
	EquipmentArray.MarkArrayDirty();
	RefreshCurrentWeaponAnimProfile();
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item)
{
	BroadcastEquipmentEquipped(Item);
	RefreshCurrentWeaponAnimProfile();

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter && OwnerCharacter->GetNetMode() != NM_DedicatedServer)
	{
		StartLocalSpawnEquipment(Item);
	}
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item)
{
	BroadcastEquipmentUnequipped(Item);
	RefreshCurrentWeaponAnimProfile(Item.ItemDef);

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
}

void UProject_JEquipmentManagerComponent::RefreshCurrentWeaponAnimProfile(const UProject_JEquipmentItemDefinition* ExcludedItemDef)
{
	AProject_JPlayerCharacter* OwnerPlayer = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!OwnerPlayer)
	{
		return;
	}

	UProject_JWeaponAnimProfile* SelectedWeaponAnimProfile = nullptr;
	for (int32 Index = EquipmentArray.Items.Num() - 1; Index >= 0; --Index)
	{
		const UProject_JEquipmentItemDefinition* ItemDef = EquipmentArray.Items[Index].ItemDef;
		if (!ItemDef || ItemDef == ExcludedItemDef || EquipmentArray.Items[Index].Slot != EProject_JEquipmentSlot::Weapon || !ItemDef->WeaponAnimProfile)
		{
			continue;
		}

		SelectedWeaponAnimProfile = ItemDef->WeaponAnimProfile;
		break;
	}

	OwnerPlayer->SetCurrentWeaponAnimProfile(SelectedWeaponAnimProfile);
}

void UProject_JEquipmentManagerComponent::BroadcastEquipmentEquipped(const FProject_JEquipmentArrayItem& Item)
{
	if (Item.ItemDef)
	{
		OnEquipmentEquipped.Broadcast(Item.Slot, Item.ItemDef);
	}
}

void UProject_JEquipmentManagerComponent::BroadcastEquipmentUnequipped(const FProject_JEquipmentArrayItem& Item)
{
	if (Item.ItemDef)
	{
		OnEquipmentUnequipped.Broadcast(Item.Slot, Item.ItemDef);
	}
}

void UProject_JEquipmentManagerComponent::ApplyEquipmentStatModifiers(const UProject_JEquipmentItemDefinition* ItemDef, float Sign) const
{
	AActor* OwnerActor = GetOwner();
	UAbilitySystemComponent* ASC = OwnerActor ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor) : nullptr;
	if (!ASC || !ItemDef || !OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	for (const FProject_JEquipmentStatModifier& Modifier : ItemDef->StatModifiers)
	{
		if (FMath::IsNearlyZero(Modifier.Value))
		{
			continue;
		}

		const float SignedValue = Modifier.Value * Sign;
		switch (Modifier.Stat)
		{
		case EProject_JEquipmentStat::MaxHealth:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetMaxHealthAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::MaxMana:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetMaxManaAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::AttackPower:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetAttackPowerAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		case EProject_JEquipmentStat::Defense:
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetDefenseAttribute(), EGameplayModOp::Additive, SignedValue);
			break;
		default:
			break;
		}
	}

	if (const UProject_JAttributeSet* AttributeSet = Cast<UProject_JAttributeSet>(ASC->GetAttributeSet(UProject_JAttributeSet::StaticClass())))
	{
		if (AttributeSet->GetHealth() > AttributeSet->GetMaxHealth())
		{
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, AttributeSet->GetMaxHealth());
		}

		if (AttributeSet->GetMana() > AttributeSet->GetMaxMana())
		{
			ASC->ApplyModToAttribute(UProject_JAttributeSet::GetManaAttribute(), EGameplayModOp::Override, AttributeSet->GetMaxMana());
		}
	}
}

int32 UProject_JEquipmentManagerComponent::FindEquipmentIndexByItem(const UProject_JEquipmentItemDefinition* ItemDef) const
{
	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].ItemDef == ItemDef)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UProject_JEquipmentManagerComponent::FindEquipmentIndexBySlot(EProject_JEquipmentSlot Slot) const
{
	if (Slot == EProject_JEquipmentSlot::None)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].Slot == Slot)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}
