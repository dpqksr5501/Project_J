#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "Engine/EngineTypes.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"

namespace
{
class FProjectJWarningMarkerSceneProxy final : public FPrimitiveSceneProxy
{
public:
    explicit FProjectJWarningMarkerSceneProxy(const UProject_JGameplayWarningMarkerComponent* Component)
        : FPrimitiveSceneProxy(Component)
    {
        Lines.Append(Component->BatchedLines);
        bWillEverBeLit = false;
    }
    virtual SIZE_T GetTypeHash() const override
    {
        static uint8 Type;
        return reinterpret_cast<SIZE_T>(&Type);
    }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap, FMeshElementCollector& Collector) const override
    {
        const FMatrix& Transform = GetLocalToWorld();
        for (int32 Index = 0; Index < Views.Num(); ++Index)
        {
            if (!(VisibilityMap & (1u << Index))) { continue; }
            // Normal runtime PDI in every build. No debug-only PDI/showflag or worker UObject access.
            FPrimitiveDrawInterface* PDI = Collector.GetPDI(Index);
            for (const FBatchedLine& Line : Lines)
            {
                PDI->DrawTranslucentLine(Transform.TransformPosition(Line.Start), Transform.TransformPosition(Line.End),
                    Line.Color, Line.DepthPriority, Line.Thickness);
            }
        }
    }
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance Relevance;
        Relevance.bDrawRelevance = IsShown(View);
        Relevance.bDynamicRelevance = true;
        Relevance.bSeparateTranslucency = Relevance.bNormalTranslucency = true;
        return Relevance;
    }
    virtual uint32 GetMemoryFootprint() const override
    {
        return static_cast<uint32>(sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize() + Lines.GetAllocatedSize());
    }
private:
    TArray<FBatchedLine, TInlineAllocator<20>> Lines;
};
}

UProject_JGameplayWarningMarkerComponent::UProject_JGameplayWarningMarkerComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
    bUseEditorCompositing = false;
    bAllowCullDistanceVolume = false;
    DefaultLifeTime = -1;
    BatchedLines.Reserve(20);
    const FVector Center(0, 0, 100);
    const FLinearColor Color(1.0f, 0.3f, 0.0f);
    for (int32 Index = 0; Index < 16; ++Index)
    {
        const float A = Index * UE_TWO_PI / 16;
        const float B = (Index + 1) * UE_TWO_PI / 16;
        BatchedLines.Emplace(Center + FVector(FMath::Cos(A), FMath::Sin(A), 0) * 45,
            Center + FVector(FMath::Cos(B), FMath::Sin(B), 0) * 45, Color, -1, 4, SDPG_Foreground);
    }
    BatchedLines.Emplace(Center + FVector(0, 0, 15), Center + FVector(0, 0, 75), Color, -1, 6, SDPG_Foreground);
    BatchedLines.Emplace(Center + FVector(-5, 0, 5), Center + FVector(5, 0, 5), Color, -1, 6, SDPG_Foreground);
    BatchedLines.Emplace(Center + FVector(0, -5, 5), Center + FVector(0, 5, 5), Color, -1, 6, SDPG_Foreground);
    BatchedLines.Emplace(Center, Center + FVector(0, 0, 10), Color, -1, 6, SDPG_Foreground);
}

FPrimitiveSceneProxy* UProject_JGameplayWarningMarkerComponent::CreateSceneProxy()
{
    return new FProjectJWarningMarkerSceneProxy(this);
}

FBoxSphereBounds UProject_JGameplayWarningMarkerComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    return FBoxSphereBounds(FBox(FVector(-45, -45, 100), FVector(45, 45, 175))).TransformBy(LocalToWorld);
}
