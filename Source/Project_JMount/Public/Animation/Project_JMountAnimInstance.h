#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Project_JMountAnimInstance.generated.h"
UCLASS(Abstract, Blueprintable)
class PROJECT_JMOUNT_API UProject_JMountAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(BlueprintReadOnly, Category="Mount") float Speed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Mount") float VerticalSpeed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Mount") bool bIsFalling = false;
	UPROPERTY(BlueprintReadOnly, Category="Mount") bool bIsDead = false;
};
