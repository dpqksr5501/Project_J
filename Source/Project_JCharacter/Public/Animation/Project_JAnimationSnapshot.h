#pragma once
#include "CoreMinimal.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
#include "Animation/Project_JPlayerAirTypes.h"
#include "Combat/Project_JCombatTypes.h"
#include "GameplayTagContainer.h"
#include "Project_JAnimationSnapshot.generated.h"

// Completed GT inputs only. No gameplay object lookup occurs during graph traversal.
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMovementThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FVector Velocity = FVector::ZeroVector;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FVector Acceleration = FVector::ZeroVector;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FTransformTrajectory Trajectory;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FVector FutureTrajectoryVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float FutureTrajectoryTurnAngle = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float GroundSpeed = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float RelativeVelocityDirection = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bHasTrajectory = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimInputThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bHasMoveInput = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float MovementDirection = 0.f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimGroundThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bWantsSprint = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimAirThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bIsInAir = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bIsJumping = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bIsFallOffStart = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimCombatThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsCombatMode = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsAttacking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsDodging = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsHitReacting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPlayingCombatIntro = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPlayingCombatOutro = false;

	/** Continuous animation composition selected by the equipped weapon profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JCombatAnimationPresentationMode PresentationMode = EProject_JCombatAnimationPresentationMode::UpperBodyOverlay;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMontageThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float FullBodyMontageWeight = 0.f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimLocomotionContextThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float DesiredFacingYaw = 0.f;
};

// Representative active GASP Blend Stack player; this does not assert final-pose contribution.
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimPlaybackSnapshot
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 int32 ProducerNodeIndex = INDEX_NONE;
 UPROPERTY()
 uint64 CaptureFrame = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float CachedNodeWeight = 0.f;
 UPROPERTY()
 uint64 ResultLastChangedFrame = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bResultChangedSincePreviousCapture = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FName SelectedAnimation = NAME_None;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float SelectedAnimationTime = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float SelectedAnimationLength = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float WantedPlayRate = 1.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bNewBlend = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimAimThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimPitch = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimOffsetAlpha = 0.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMountThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float Speed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FVector LeftHandTargetComponentSpace = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FVector RightHandTargetComponentSpace = FVector::ZeroVector;

	/** Profile-defined pose family/features; copied on the game thread. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FGameplayTagContainer AnimationTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float TransitionBlendTime = 0.2f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsMounted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsFlying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsGliding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bHasHandIKTargets = false;

};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimThreadSafeData
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 float DeltaTime = 0.f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 bool bIsLocallyControlled = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimMovementThreadSafeData Movement;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimInputThreadSafeData Input;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimGroundThreadSafeData Ground;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimAirThreadSafeData Air;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimCombatThreadSafeData Combat;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimMontageThreadSafeData Montage;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimLocomotionContextThreadSafeData LocomotionContext;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimAimThreadSafeData Aim;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 FProject_JAnimMountThreadSafeData Mount;
 UPROPERTY()
 FProject_JPlayerAirFrame PlayerLocomotionAir;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation|Snapshot")
 EProject_JAnimationLocomotionMode LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;
};
