#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JWorldEnvironment.generated.h"
class AProject_JWorldState;
class ADirectionalLight;
class AExponentialHeightFog;
class UMaterialParameterCollection;

/** Client presentation only. Server weather never depends on sky/particle actors. */
UCLASS()
class PROJECT_J_API AProject_JWorldEnvironment : public AActor
{
    GENERATED_BODY()
public:
    AProject_JWorldEnvironment();
    UPROPERTY(EditAnywhere, Category="Environment") TObjectPtr<AProject_JWorldState> State;
    UPROPERTY(EditAnywhere, Category="Environment") TObjectPtr<ADirectionalLight> Sun;
    UPROPERTY(EditAnywhere, Category="Environment") TObjectPtr<AExponentialHeightFog> Fog;
    UPROPERTY(EditAnywhere, Category="Environment") TObjectPtr<UMaterialParameterCollection> Parameters;
    /** Preserve old authored maps by default; new physical lighting sets its measured lux value. */
    UPROPERTY(EditAnywhere, Category="Environment", meta=(ClampMin="0")) float ClearSunIntensity = 6.f;
    UPROPERTY(EditAnywhere, Category="Environment", meta=(ClampMin="0",ClampMax="1")) float RainSunIntensityMultiplier = 0.25f;
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
};
