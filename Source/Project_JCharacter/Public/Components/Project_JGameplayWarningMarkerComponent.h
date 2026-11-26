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
    static constexpr double GroundClearanceCm = 8.0;
    UProject_JGameplayWarningMarkerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    /** Configure before registration. Ground warning is occluded by level geometry. */
    void SetGroundCircle(float Radius, const FTransform& Surface);
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
private:
    // The scene proxy draws in component space. Cache once when either shape is authored.
    void CacheLocalLineBounds();
    FBoxSphereBounds LocalLineBounds{ForceInit};
};
