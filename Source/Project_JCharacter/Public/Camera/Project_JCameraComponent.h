// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Project_JCameraComponent.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;

/**
 * 전담 카메라 컴포넌트
 * 캐릭터(God Class)로부터 스프링암과 카메라 제어 로직을 독립시킨 컴포넌트입니다.
 * 원격 프록시(Simulated Proxy)일 경우 최적화를 위해 틱과 카메라를 비활성화합니다.
 */
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JCameraComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	void Initialize(USpringArmComponent* InCameraBoom, UCameraComponent* InFollowCamera);
	void RefreshAbilitySystemBinding();

	UPROPERTY()
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY()
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 평상시 카메라 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Zoom")
	float NormalTargetArmLength = 400.0f;

	/** 전투 모드 시 카메라 거리 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Zoom")
	float CombatTargetArmLength = 300.0f;

	/** 줌인/아웃 보간 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Zoom")
	float ZoomInterpolationSpeed = 5.0f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** GAS 상태 변경 콜백 (전투 모드 전환 감지) */
	UFUNCTION()
	void OnCombatStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	void UnregisterAbilitySystemBinding();

	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;
	FDelegateHandle CombatModeTagEventHandle;
	bool bIsCombatMode = false;
	bool bIsLocallyControlled = false;
};
