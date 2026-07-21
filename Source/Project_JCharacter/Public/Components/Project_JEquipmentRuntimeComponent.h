// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "ActiveGameplayEffectHandle.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Project_JEquipmentRuntimeComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JEquipmentManagerComponent;
class UProject_JModularMeshComponent;
class UProject_JCombatStyleDefinition;
class UProject_JWeaponPresentationProfile;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FProject_JEquipmentRuntimeItem
{
	GENERATED_BODY()

	UPROPERTY()
	UProject_JEquipmentItemDefinition* ItemDef = nullptr;

	UPROPERTY()
	UProject_JModularMeshComponent* SpawnedMesh = nullptr;

	/** Granted Ability handles for this item */
	UPROPERTY(Transient)
	FProject_JAbilitySet_GrantedHandles GrantedHandles;

	UPROPERTY(Transient)
	TArray<FActiveGameplayEffectHandle> GrantedEffectHandles;

	UPROPERTY(Transient)
	bool bAppliedStatModifierFallback = false;
};

/**
 * Handles the visual and runtime representation (Mesh, ASC Grants, Stats) 
 * of equipment on the character. Listens to the PlayerState's EquipmentManager.
 */
UCLASS(ClassGroup=(Equipment), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JEquipmentRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UProject_JEquipmentRuntimeComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Binds to a specific equipment manager (usually on PlayerState) */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void BindToEquipmentManager(UProject_JEquipmentManagerComponent* InEquipmentManager);

protected:
	UFUNCTION()
	void OnEquipmentEquipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION()
	void OnEquipmentUnequipped(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef);

private:
	void ApplyEquipmentGameplay(ACharacter& OwnerCharacter, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const;
	void RemoveEquipmentGameplay(ACharacter& OwnerCharacter, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const;
	void StartLocalSpawnEquipment(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef);
	void OnEquipmentMeshLoaded(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* ItemDef);
	void DestroyEquipmentVisual(FProject_JEquipmentRuntimeItem& RuntimeItem) const;
	UProject_JCombatStyleDefinition* ResolveCurrentCombatStyle() const;
	UProject_JWeaponPresentationProfile* ResolveCurrentWeaponPresentationProfile() const;
	void ApplyEquipmentEffects(UAbilitySystemComponent& ASC, const UProject_JEquipmentItemDefinition& ItemDef, FProject_JEquipmentRuntimeItem& RuntimeItem) const;
	void RemoveEquipmentEffects(UAbilitySystemComponent& ASC, FProject_JEquipmentRuntimeItem& RuntimeItem) const;
	void ApplyEquipmentStatModifiers(const UProject_JEquipmentItemDefinition* ItemDef, float Sign) const;
	void RefreshCurrentWeaponConfiguration();

private:
	UPROPERTY(Transient)
	UProject_JEquipmentManagerComponent* BoundEquipmentManager;

	UPROPERTY(Transient)
	TMap<EProject_JEquipmentSlot, FProject_JEquipmentRuntimeItem> RuntimeItems;
};
