// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Project_JMMOTypes.h"
#include "Project_JGameState.generated.h"

/**
 * Public world or instance state visible to every connected player.
 *
 * This should stay intentionally thin. Large lists, private service data, and
 * backend search results should be fetched by service/UI layers instead.
 */
UCLASS()
class PROJECT_J_API AProject_JGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "MMO|World")
	FProject_JWorldInstanceId GetWorldInstanceId() const { return WorldInstanceId; }

	UFUNCTION(BlueprintPure, Category = "MMO|World")
	FName GetPublicEventId() const { return PublicEventId; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|World")
	void SetWorldInstanceId(const FProject_JWorldInstanceId& InWorldInstanceId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MMO|World")
	void SetPublicEventId(FName InPublicEventId);

protected:
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|World")
	FProject_JWorldInstanceId WorldInstanceId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MMO|World")
	FName PublicEventId = NAME_None;
};
