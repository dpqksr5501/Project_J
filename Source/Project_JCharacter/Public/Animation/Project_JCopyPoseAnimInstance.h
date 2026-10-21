#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Project_JCopyPoseAnimInstance.generated.h"

/** Shared base pose for modular parts. Mesh post-process animation owns extra-bone dynamics. */
UCLASS(Transient, NotBlueprintable)
class PROJECT_JCHARACTER_API UProject_JCopyPoseAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
