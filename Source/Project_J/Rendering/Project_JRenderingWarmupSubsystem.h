#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JRenderingWarmupSubsystem.generated.h"

/** Requests engine-owned PSOs before the first map renders. Never blocks GT. */
UCLASS()
class UProject_JRenderingWarmupSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
