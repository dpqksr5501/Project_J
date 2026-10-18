#include "World/Project_JWorldEnvironment.h"
#include "World/Project_JWorldState.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/World.h"

AProject_JWorldEnvironment::AProject_JWorldEnvironment()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
#if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded = false;
#endif
}
void AProject_JWorldEnvironment::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(GetNetMode() != NM_DedicatedServer);
}
void AProject_JWorldEnvironment::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsValid(State)) { return; }
    const float Rain = State->GetRain();
    const float Hour = State->GetHour();
    if (IsValid(Sun))
    {
        Sun->SetActorRotation(FRotator((Hour-6.f)*-15.f, -25.f, 0));
        const float Daylight = FMath::Clamp(FMath::Sin((Hour-6.f)*PI/12.f)*5.f, 0.f, 1.f);
        const float RainMultiplier = FMath::Lerp(1.f, FMath::Clamp(RainSunIntensityMultiplier, 0.f, 1.f), Rain);
        CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->SetIntensity(Daylight*FMath::Max(0.f, ClearSunIntensity)*RainMultiplier);
    }
    if (IsValid(Fog)) { Fog->GetComponent()->SetFogDensity(FMath::Lerp(0.005f, 0.025f, Rain)); }
    if (Parameters)
    {
        UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(Parameters);
        Instance->SetScalarParameterValue(TEXT("Rain"), Rain);
        Instance->SetScalarParameterValue(TEXT("WorldHour"), Hour);
    }
}
