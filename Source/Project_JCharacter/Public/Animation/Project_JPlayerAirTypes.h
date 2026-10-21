#pragma once
#include "CoreMinimal.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JPlayerAirTypes.generated.h"
class UChooserTable;
/** Value context for the project's authored jump/fall clips. No actor or mutable gameplay state. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerAirQuery
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, Category="Air") EProject_JStateControllerPresentationState State=EProject_JStateControllerPresentationState::TransitionToInAir;
 UPROPERTY(EditAnywhere, Category="Air") EProject_JLocomotionRotationMode RotationMode=EProject_JLocomotionRotationMode::OrientToMovement;
 UPROPERTY(EditAnywhere, Category="Air") EProject_JLocomotionGaitIntent Gait=EProject_JLocomotionGaitIntent::Run;
 UPROPERTY(EditAnywhere, Category="Air") EProject_JStateControllerStrafeDirection Direction=EProject_JStateControllerStrafeDirection::Forward;
 UPROPERTY(EditAnywhere, Category="Air") EProject_JStateControllerFoot Foot=EProject_JStateControllerFoot::Right;
 UPROPERTY(EditAnywhere, Category="Air") bool bFallOff=false;
};
USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JPlayerAirFrame
{
 GENERATED_BODY()
 UPROPERTY() FProject_JPlayerAirQuery Query;
 UPROPERTY() TObjectPtr<UChooserTable> Chooser;
 UPROPERTY() uint64 Episode=0;
 UPROPERTY() double Seconds=0.;
 UPROPERTY() float LiveDirectionDegrees=0.f;
 UPROPERTY() bool bHasLiveDirection=false;
 UPROPERTY() bool bEnableReselection=true;
 UPROPERTY() float MaxProgressRatio=.85f;
 UPROPERTY() float MinBlendProgressRatio=.7f;
 UPROPERTY() float BlendTime=.15f;
 UPROPERTY() float Cooldown=.08f;
};

/** Selection uses admitted movement values; a physical jump keeps its foot and playhead. */
namespace ProjectJ::Animation::Air
{
 PROJECT_JCHARACTER_API float ResolveLiveAngle(const FVector& Velocity, bool bHasInput, float InputAngle,
  float ActorYaw, float DesiredFacingYaw, EProject_JLocomotionRotationMode RotationMode);
 PROJECT_JCHARACTER_API FProject_JPlayerAirQuery ResolveReselectionQuery(
  const FProject_JPlayerAirQuery& Live, const FProject_JPlayerAirQuery& Committed, float LiveAngle);
}
