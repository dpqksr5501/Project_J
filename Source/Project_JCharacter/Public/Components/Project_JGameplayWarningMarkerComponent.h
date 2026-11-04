#pragma once

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Project_JGameplayWarningMarkerComponent.generated.h"

/** Fixed local-space attention beacon. One per presentation owner; no asset, animation or component tick. */
UCLASS(Transient, NotBlueprintable)
class PROJECT_JCHARACTER_API UProject_JGameplayWarningMarkerComponent final : public ULineBatchComponent
{
    GENERATED_BODY()
public:
    UProject_JGameplayWarningMarkerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};
