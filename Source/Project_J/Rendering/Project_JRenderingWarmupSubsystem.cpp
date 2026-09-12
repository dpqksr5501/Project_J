#include "Rendering/Project_JRenderingWarmupSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"
#include "PSOPrecacheMaterial.h"
#include "Misc/App.h"

namespace
{
TAutoConsoleVariable<int32> CVarDefaultLightFunction(TEXT("ProjectJ.Rendering.PrecacheDefaultLightFunction"), 1,
    TEXT("Request engine default light-function PSOs at game-instance initialization. Set to 0 before startup for comparison."));
}

void UProject_JRenderingWarmupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
#if UE_WITH_PSO_PRECACHING
    if (!FApp::CanEverRender() || !IsComponentPSOPrecachingEnabled() || !CVarDefaultLightFunction.GetValueOnGameThread()) { return; }
    // UE 5.8 LightComponent only requests material PSOs when an authored light
    // function is assigned. TLV also uses the engine's default light function.
    // Reuse the public engine collector; do not duplicate renderer permutations.
    auto* Material = UMaterial::GetDefaultMaterial(MD_LightFunction);
    if (!Material) { return; }
    Material->ConditionalPostLoad();
    FMaterialInterfacePSOPrecacheParams Params;
    Params.Priority = EPSOPrecachePriority::High;
    Params.MaterialInterface = Material;
    Params.VertexFactoryDataList.Add(&FLocalVertexFactory::StaticType);
    TArray<FMaterialPSOPrecacheRequestID> Requests;
    FGraphEventArray Events;
    PrecacheMaterialPSOs({Params}, Requests, Events);
    // Engine material/PSO caches own request lifetime and deduplicate repeated
    // game instances. No callback captures this subsystem and no join is needed
    // on map teardown. First-frame readiness still needs runtime measurement.
#endif
}
