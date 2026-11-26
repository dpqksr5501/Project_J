#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "Combat/Project_JCombatQueryTypes.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
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
    CacheLocalLineBounds();
}

FPrimitiveSceneProxy* UProject_JGameplayWarningMarkerComponent::CreateSceneProxy()
{
    return new FProjectJWarningMarkerSceneProxy(this);
}

void UProject_JGameplayWarningMarkerComponent::SetGroundCircle(float Radius, const FTransform& Surface)
{
    check(!IsRegistered());
    BatchedLines.Reset();
    constexpr int32 SegmentCount = 32;
    const FLinearColor Color(1.0f, 0.3f, 0.0f);
    // Intersect each ray on the impact plane with the authoritative circle's horizontal and vertical limits.
    const double HalfHeight = Project_J::Combat::GetCircleHalfHeight(Radius);
    struct FEdgePoint { FVector Position; bool bSupported; };
    UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJGroundWarningSupport), false, GetOwner());
    const auto PointOnSurface = [&](int32 Segment) -> FEdgePoint
    {
        const double Angle = Segment * UE_DOUBLE_TWO_PI / SegmentCount;
        const FVector Tangent(FMath::Cos(Angle), FMath::Sin(Angle), 0);
        const FVector WorldDirection = Surface.TransformVectorNoScale(Tangent);
        const double Distance = FMath::Min(Radius / FMath::Max(WorldDirection.Size2D(), UE_DOUBLE_SMALL_NUMBER),
            HalfHeight / FMath::Max(FMath::Abs(WorldDirection.Z), UE_DOUBLE_SMALL_NUMBER));
        const FVector Predicted = Surface.TransformPosition(Tangent * Distance);
        // A short probe follows treads near the cast plane without jumping to a different storey.
        // Unloaded collision and unsupported edges keep a dashed range outline.
        const FVector Probe(0, 0, 50);
        FHitResult Hit;
        if (World && World->LineTraceSingleByObjectType(Hit, Predicted + Probe, Predicted - Probe,
            FCollisionObjectQueryParams(ECC_WorldStatic), Query) && !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0)
        {
            return {Surface.InverseTransformPosition(Hit.ImpactPoint + FVector(0, 0, GroundClearanceCm)), true};
        }
        return {Tangent * Distance, false};
    };
    const FEdgePoint First = PointOnSurface(0);
    FEdgePoint Start = First;
    for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
    {
        const FEdgePoint End = Segment == SegmentCount ? First : PointOnSurface(Segment);
        const FVector VisibleEnd = Start.bSupported && End.bSupported ? End.Position
            : FMath::Lerp(Start.Position, End.Position, 0.4);
        BatchedLines.Emplace(Start.Position, VisibleEnd, Color, -1, 5, SDPG_World);
        Start = End;
    }
    CacheLocalLineBounds();
}

void UProject_JGameplayWarningMarkerComponent::CacheLocalLineBounds()
{
    FBox Box(ForceInit);
    for (const FBatchedLine& Line : BatchedLines)
    {
        Box += Line.Start;
        Box += Line.End;
    }
    LocalLineBounds = FBoxSphereBounds(Box.ExpandBy(1.0));
}

FBoxSphereBounds UProject_JGameplayWarningMarkerComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    return LocalLineBounds.TransformBy(LocalToWorld);
}
