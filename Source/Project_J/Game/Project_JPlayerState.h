// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Project_JMMOTypes.h"
#include "AbilitySystemInterface.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "Project_JPlayerState.generated.h"

class UProject_JInventoryComponent;
class UProject_JEquipmentManagerComponent;

/**
 * Public, replicated player metadata.
 *
 * Keep private account data, inventory, currencies, and long-lived service data
 * out of PlayerState. Those belong to backend-owned session data or owner-only
 * replicated components.
 */
UCLASS()
class PROJECT_J_API AProject_JPlayerState : public APlayerState, public IAbilitySystemInterface, public IProject_JAbilitySystemOwnerInterface
{
	GENERATED_BODY()

public:
	AProject_JPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Implement IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// Implement IProject_JAbilitySystemOwnerInterface
	virtual UProject_JAbilitySystemComponent* GetProjectJAbilitySystemComponent() const override;
	virtual UProject_JAttributeSet* GetProjectJAttributeSet() const override;
	virtual bool HasGrantedDefaultAbilities() const override { return bDefaultAbilitiesGranted; }
	virtual void SetHasGrantedDefaultAbilities(bool bGranted) override { bDefaultAbilitiesGranted = bGranted; }

	UFUNCTION(BlueprintPure, Category = "MMO|Identity")
	FProject_JAccountId GetAccountId() const { return AccountId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Identity")
	FProject_JCharacterId GetCharacterId() const { return CharacterId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Character")
	FName GetPublicClassId() const { return PublicClassId; }

	UFUNCTION(BlueprintPure, Category = "MMO|Character")
	int32 GetPublicCharacterLevel() const { return PublicCharacterLevel; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Identity")
	void SetIdentity(const FProject_JAccountId& InAccountId, const FProject_JCharacterId& InCharacterId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|Character")
	void SetPublicCharacterSnapshot(FName InClassId, int32 InLevel);

	FORCEINLINE UProject_JInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }
	FORCEINLINE UProject_JEquipmentManagerComponent* GetEquipmentManagerComponent() const { return EquipmentManagerComponent; }

protected:
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JAccountId AccountId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JCharacterId CharacterId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	FName PublicClassId = NAME_None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	int32 PublicCharacterLevel = 1;

private:
	UPROPERTY(Transient)
	bool bDefaultAbilitiesGranted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	class UProject_JAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	class UProject_JAttributeSet* AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProject_JInventoryComponent* InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProject_JEquipmentManagerComponent* EquipmentManagerComponent;
};
