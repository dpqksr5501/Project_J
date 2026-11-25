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
        const float TargetPitch = (Hour - 6.f) * -15.f;
        const float Step = FMath::Max(0.f, SunRotationStepDegrees);
        const float SnappedPitch = Step > UE_SMALL_NUMBER ? FMath::GridSnap(TargetPitch, Step) : TargetPitch;
        if (!bHasAppliedSunPitch || !FMath::IsNearlyEqual(SnappedPitch, LastAppliedSunPitch, 1e-3f))
        {
            Sun->SetActorRotation(FRotator(SnappedPitch, -25.f, 0));
            LastAppliedSunPitch = SnappedPitch;
            bHasAppliedSunPitch = true;
        }
        const float Daylight = FMath::Clamp(FMath::Sin((Hour-6.f)*PI/12.f)*5.f, 0.f, 1.f);
        const float RainMultiplier = FMath::Lerp(1.f, FMath::Clamp(RainSunIntensityMultiplier, 0.f, 1.f), Rain);
        if (auto* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
        {
            Light->SetIntensity(Daylight*FMath::Max(0.f, ClearSunIntensity)*RainMultiplier);
        }
    }
    if (IsValid(Fog))
    {
        if (auto* FogComp = Fog->GetComponent())
        {
            FogComp->SetFogDensity(FMath::Lerp(0.005f, 0.025f, Rain));
        }
    }
    if (Parameters && GetWorld())
    {
        if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(Parameters))
        {
            Instance->SetScalarParameterValue(TEXT("Rain"), Rain);
            Instance->SetScalarParameterValue(TEXT("WorldHour"), Hour);
        }
    }
}
