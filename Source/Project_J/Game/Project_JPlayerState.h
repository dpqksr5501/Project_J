// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Project_JMMOTypes.h"
#include "Project_JPlayerState.generated.h"

/**
 * Public, replicated player metadata.
 *
 * Keep private account data, inventory, currencies, and long-lived service data
 * out of PlayerState. Those belong to backend-owned session data or owner-only
 * replicated components.
 */
UCLASS()
class PROJECT_J_API AProject_JPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

protected:
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JAccountId AccountId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Identity")
	FProject_JCharacterId CharacterId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	FName PublicClassId = NAME_None;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|Character")
	int32 PublicCharacterLevel = 1;
};
