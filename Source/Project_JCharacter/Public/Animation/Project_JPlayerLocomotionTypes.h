#pragma once
#include "CoreMinimal.h"
#include "Project_JLocomotionAnimTypes.h"
#include "AlphaBlend.h"
#include "Animation/TrajectoryTypes.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "Project_JPlayerLocomotionTypes.generated.h"
class UAnimationAsset;
class UBlendProfile;

/** Predicates sampled by the state controller, with its own source-state clock. */
UENUM(BlueprintType)
enum class EProject_JPlayerTransitionRule : uint8
{
 Moving, Idle, AirTransitionComplete, NoValidAnimationOrLooping, StanceChanged,
 NewPivot, LocomotionSettingsChanged, TransitionPivot, InAir, Grounded,
 IdleTurnRedirect, EarlyFacingRedirect, IdleBreakElapsed, NotifyRedirect,
 NotifyLoop, Circling, TrajectoryDiscontinuity, MovementDirectionChanged, SlopeChanged
};

UENUM(BlueprintType)
enum class EProject_JPlayerMovementMode : uint8
{
 OnGround = 0,
 InAir = 1,
 Sliding = 2,
 Traversing = 3,
 Ragdoll = 4,
 Flying = 5,
};

UENUM(BlueprintType)
enum class EProject_JPlayerRotationMode : uint8
{
 OrientToMovement = 0,
 Strafe = 1,
 Aim = 2,
};

UENUM(BlueprintType)
enum class EProject_JPlayerMovementState : uint8
{
 Idle = 0,
 Moving = 1,
};

UENUM(BlueprintType)
enum class EProject_JPlayerStance : uint8
{
 Stand = 0,
 Crouch = 1,
};

UENUM(BlueprintType)
enum class EProject_JPlayerMovementDirection : uint8
{
 Forward = 0,
 Backward = 1,
 LeftLeftFootForward = 2,
 LeftRightFootForward = 3,
 RightLeftFootForward = 4,
 RightRightFootForward = 5,
};

/** Presentation curve query shared by Mover input production and its authored Chooser. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerRotationOffsetQuery
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerMovementMode MovementMode = EProject_JPlayerMovementMode::OnGround;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerMovementDirection MovementDirection = EProject_JPlayerMovementDirection::Forward;
};

UENUM(BlueprintType)
enum class EProject_JPlayerRagdollInjuryState : uint8
{
 None = 0,
 Limp = 1,
 Stunned = 2,
 HeadFace = 3,
 HeadBack = 4,
 BodyFront = 5,
 Groin = 6,
};

UENUM(BlueprintType)
enum class EProject_JPlayerPlaybackState : uint8
{
 IdleLoop = 0,
 IdleTransition = 1,
 LocomotionLoop = 2,
 LocomotionTransition = 3,
 AirLoop = 4,
 AirTransition = 5,
 IdleBreak = 6,
 SlideTransition = 7,
 SlideLoop = 8,
 DebugFlyingLoop = 9,
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerPlayerInputState
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool WantsToSprint = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool WantsToWalk = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool WantsToStrafe = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool WantsToAim = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool WantsToCrouch = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerCharacterPropertiesForAnimation
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FProject_JPlayerPlayerInputState InputState = {};
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerMovementMode MovementMode = EProject_JPlayerMovementMode::OnGround;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerStance Stance = EProject_JPlayerStance::Stand;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerRotationMode RotationMode = EProject_JPlayerRotationMode::OrientToMovement;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JLocomotionGaitIntent Gait = EProject_JLocomotionGaitIntent::Walk;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerMovementDirection MovementDirection = EProject_JPlayerMovementDirection::Forward;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FTransform ActorTransform = FTransform::Identity;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FVector Velocity = FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FVector InputAcceleration = FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double CurrentMaxAcceleration = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FRotator OrientationIntent = FRotator::ZeroRotator;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FRotator AimingRotation = FRotator::ZeroRotator;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FVector LandVelocity = FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FVector GroundNormal = FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FTransform BasedMovementDelta = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerRagdollProperties
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double Ragdoll_Speed = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double Ragdoll_TimeToImpact = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double Ragdoll_RollAmount = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FVector2D Ragdoll_ImpactDirection = FVector2D::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EProject_JPlayerRagdollInjuryState Ragdoll_InjuryState = EProject_JPlayerRagdollInjuryState::None;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerBlendStackInputs
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") TObjectPtr<UAnimationAsset> Anim = {};
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") bool Loop = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double StartTime = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double BlendTime = 0.000000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EAlphaBlendOption BlendCurve = EAlphaBlendOption::Linear;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") TObjectPtr<UBlendProfile> BlendProfile = {};
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") TArray<FName> Tags = {};
};

/** Typed output of the ground Chooser. A profile name is resolved on the canonical skeleton. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JPlayerGroundChooserOutput
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double StartTime=0.;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") double BlendTime=.3;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") EAlphaBlendOption BlendCurve=EAlphaBlendOption::QuadraticInOut;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") FName BlendProfile=NAME_None;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player Locomotion") TArray<FName> Tags;
};
