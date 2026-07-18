#pragma once
#include "Animation/Project_JMountAnimInstance.h"
#include "Project_JFlyingMountAnimInstance.generated.h"
UCLASS(Abstract, Blueprintable)
class PROJECT_JMOUNT_API UProject_JFlyingMountAnimInstance : public UProject_JMountAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(BlueprintReadOnly, Category="Mount|Flight") bool bIsFlying = false;
	UPROPERTY(BlueprintReadOnly, Category="Mount|Flight") bool bIsGliding = false;
};
