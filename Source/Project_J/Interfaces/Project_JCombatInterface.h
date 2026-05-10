// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JCombatInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UProject_JCombatInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for characters or actors that can participate in combat.
 */
class PROJECT_J_API IProject_JCombatInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	// 예시: 캐릭터 레벨 반환
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	int32 GetCharacterLevel() const;

	// 예시: 무기나 공격 이펙트가 스폰될 소켓의 위치 반환
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	FVector GetCombatSocketLocation(const FName& SocketName);

	// 예시: 사망 상태 확인
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	bool IsDead() const;
};
