#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "World/Project_JWorldDefinition.h"
#include "Project_JWorldState.generated.h"

USTRUCT(BlueprintType)
struct FProject_JEnvironmentState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) float FromRain = 0;
    UPROPERTY(BlueprintReadOnly) float ToRain = 0;
    UPROPERTY(BlueprintReadOnly) double TransitionStart = 0;
    UPROPERTY(BlueprintReadOnly) float TransitionSeconds = 20;
    UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

/** Small world-scoped public snapshot. Always-loaded placement; no NPC/event arrays or persistence claims. */
UCLASS()
class PROJECT_J_API AProject_JWorldState : public AInfo
{
    GENERATED_BODY()
public:
    AProject_JWorldState();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World") TObjectPtr<UProject_JWorldDefinition> Definition;
    UPROPERTY(Replicated, BlueprintReadOnly, Category="World") FProject_JEnvironmentState Environment;
    UPROPERTY(Replicated) double ClockStart = 0;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World") bool SetRain(float Target, float Duration = 20);
    UFUNCTION(BlueprintPure, Category="World") float GetRain() const;
    UFUNCTION(BlueprintPure, Category="World") float GetHour() const;
    double GetSynchronizedSeconds() const;
    static float EvaluateRain(const FProject_JEnvironmentState& State, double Now);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    virtual void BeginPlay() override;
};
