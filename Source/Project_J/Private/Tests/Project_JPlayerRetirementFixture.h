#pragma once
#include "Game/Project_JPlayerController.h"
#include "Project_JPlayerRetirementFixture.generated.h"

UCLASS(NotBlueprintable, Transient)
class AProject_JPlayerRetirementFixture : public AProject_JPlayerController
{
    GENERATED_BODY()
public:
    void LeaveForTest() { PawnLeavingGame(); }
};
