#pragma once
#include "CoreMinimal.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JLocomotionProfile.generated.h"
class UBlendProfile;
enum class EProject_JPlayerMovementDirection : uint8;

/** Camera-relative sectors for locomotion family selection, independent of keys
 * and animation names. Hysteresis stabilizes analog input near sector borders. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JStrafeDirectionPolicy
{
 GENERATED_BODY()
 // Midpoints between the forward diagonals / side and side / rear diagonals.
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strafe", meta=(ClampMin="0.0",ClampMax="180.0",Units="deg"))
 float ForwardHalfAngle=67.5f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strafe", meta=(ClampMin="0.0",ClampMax="180.0",Units="deg"))
 float BackwardStartAngle=112.5f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Strafe", meta=(ClampMin="0.0",Units="deg"))
 float HysteresisAngle=10.f;
 bool IsValid() const;
 EProject_JPlayerMovementDirection Resolve(float SignedAngle,EProject_JPlayerMovementDirection Previous) const;
};
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JStateControllerChooserOutput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|State Controller")
	float StartTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|State Controller", meta = (ClampMin = "0.0", Units = "s"))
	float BlendTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|State Controller")
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|State Controller")
	TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerAirPolicy
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air")
 bool bEnableAirJumpReselection=true;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin=".01"))
 float AirJumpReselectCooldown=.08f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin=".05", ClampMax=".5"))
 float AirJumpBlendTime=.15f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin=".3", ClampMax="1.0"))
 float AirJumpMinBlendProgressRatio=.70f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin=".1", ClampMax="1.0"))
 float AirJumpMaxProgressRatio=.85f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin="10.0", ClampMax="90.0"))
 float OTMJumpLaunchAngleThreshold=45.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air", meta=(ClampMin="0.0"))
 float RemoteJumpUrgentAnimationUpdateDuration=.1f;
};
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JRemoteVisualLocomotionPolicy
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Remote Visual", meta=(ClampMin="0.0", Units="s"))
 float UrgentOneShotAnimationUpdateDuration=.1f;
};
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionTransitionPolicy
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Gameplay", meta=(ClampMin="0.0"))
 float IdleSpeedThreshold=30.f;
};
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JLocomotionProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Strafe")
 FProject_JStrafeDirectionPolicy StrafeDirectionPolicy;
 FName GetMovementValidationFailure() const;
 FProject_JAnimationBudgetSettings GetResolvedAnimationBudgetSettings() const { return AnimationBudget; }
#if WITH_EDITOR
 virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Air")
 FProject_JPlayerAirPolicy AirPolicy;
 /** Serialized legacy name: normal Run admission cap. Ctrl Walk is owned by Mover tuning. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Movement", meta=(ClampMin="0.0", DisplayName="Run Speed Limit"))
 float WalkSpeed=500.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Movement", meta=(ClampMin="0.0"))
 float SprintSpeed=750.f;
 /** Keep authored ground cadence unless explicitly opting into speed/turn-driven time scaling. Air and montage timing are independent. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Player|Ground")
 bool bScaleGroundPlayRateWithSpeed=false;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Gameplay")
 FProject_JLocomotionTransitionPolicy TransitionPolicy;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Remote Visual")
 FProject_JRemoteVisualLocomotionPolicy RemoteVisualPolicy;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Optimization", meta=(ClampMin="0.0"))
 float AnimStateHiddenRemoteUpdateInterval=0.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion|Optimization")
 FProject_JAnimationBudgetSettings AnimationBudget;
};
