// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JCharacterAnimInstanceBase.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JCharacterAnimInstance.generated.h"

class ACharacter;
class APawn;
class AProject_JPlayerCharacter;
class UChooserTable;
class UPoseSearchDatabase;
class UProject_JLocomotionAnimStateComponent;
class UProject_JLocomotionProfile;
class UProject_JCombatAnimProfile;
struct FAnimNode_Base;
struct FAnimationUpdateContext;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMovementThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector AccelerationDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FTransformTrajectory Trajectory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AccelerationRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWasAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStoppedAcceleratingThisFrame = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasTrajectory = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimInputThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MovementDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bSharpTurnRequested = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimGroundThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseSprintLocomotion = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimAirThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsJumping = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsFallOffStart = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimLandingThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LastFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LandStartFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsLanding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseHeavyLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasMoving = false;
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
struct PROJECT_JCHARACTER_API FProject_JAnimThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float DeltaTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimMovementThreadSafeData Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimInputThreadSafeData Input;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimGroundThreadSafeData Ground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimAirThreadSafeData Air;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimLandingThreadSafeData Landing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimCombatThreadSafeData Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimAimThreadSafeData Aim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector AccelerationDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FTransformTrajectory Trajectory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AccelerationRatio = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LastFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LandStartFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MovementDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimPitch = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimOffsetAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWasAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStoppedAcceleratingThisFrame = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsJumping = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsFallOffStart = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsLanding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseHeavyLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasTrajectory = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bSharpTurnRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseSprintLocomotion = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopWasSprinting = false;

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
	EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;

	void SyncLegacyFieldsFromStructuredData();
};

USTRUCT()
struct PROJECT_JCHARACTER_API FProject_JCharacterAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FProject_JCharacterAnimInstanceProxy()
	{
		LinkNativeGraph();
	}
	FProject_JCharacterAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
		LinkNativeGraph();
	}

	void QueueGameThreadData(
		const FProject_JAnimThreadSafeData& InData,
		UPoseSearchDatabase* InSelectedDatabase,
		bool bInMotionMatchingEnabled,
		bool bInUpdateMotionMatchingThisFrame);
	const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
	UPoseSearchDatabase* GetCurrentActiveDatabase() const { return CurrentActiveDatabase.Get(); }

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode_WithRoot(const FAnimationUpdateContext& InContext, FAnimNode_Base* InRootNode, FName InLayerName) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void LinkNativeGraph();
	void ApplySelectedDatabaseToNativeNode();

	FProject_JAnimThreadSafeData PendingGameThreadData;
	FProject_JAnimThreadSafeData ThreadSafeData;
	bool bMotionMatchingEnabled = true;
	bool bUpdateMotionMatchingThisFrame = true;

	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> CurrentActiveDatabase = nullptr;

	FAnimNode_PoseSearchHistoryCollector NativePoseHistoryNode;
	FAnimNode_MotionMatching NativeMotionMatchingNode;
};

UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstance : public UProject_JCharacterAnimInstanceBase
{
	GENERATED_BODY()

public:
	UProject_JCharacterAnimInstance();

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	UFUNCTION(BlueprintCallable, Category = "Animation|Events")
	void HandleLocomotionAnimEvent(EProject_JLocomotionAnimEvent EventType);

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use dedicated thread-safe getters such as GetThreadSafeTrajectory, GetThreadSafeAimYaw, GetThreadSafeAimPitch, and GetThreadSafeAimOffsetAlpha in AnimGraph."))
	FProject_JAnimThreadSafeData GetThreadSafeData() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	FTransformTrajectory GetThreadSafeTrajectory() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimYaw() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimPitch() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimOffsetAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementPlantSettings Get_FootPlacementPlantSettings() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementInterpolationSettings Get_FootPlacementInterpolationSettings() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Motion Matching", meta = (BlueprintThreadSafe))
	UPoseSearchDatabase* GetCurrentActivePoseSearchDatabaseThreadSafe() const;

protected:
	FProject_JAnimThreadSafeData BuildThreadSafeData(float DeltaSeconds) const;
	void FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const;
	bool FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const;
	void PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data);
	UPoseSearchDatabase* EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data) const;
	void PublishChooserProperties(const FProject_JAnimThreadSafeData& Data);
	bool ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds);
	float CalculateMotionMatchingUpdateInterval() const;
	void ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const;
	float CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const;
	bool ShouldSkipNativeUpdate(float DeltaSeconds);
	const UProject_JLocomotionProfile* GetLocomotionProfile() const;
	float GetEffectiveGenericMoveInputSpeedThreshold() const;
	float GetEffectiveSprintLocomotionSpeedThreshold() const;
	float GetEffectiveHiddenRemoteUpdateInterval() const;
	float GetEffectiveNearMotionMatchingDistance() const;
	float GetEffectiveMidMotionMatchingDistance() const;
	float GetEffectiveFarMotionMatchingDistance() const;
	float GetEffectiveMidMotionMatchingUpdateInterval() const;
	float GetEffectiveFarMotionMatchingUpdateInterval() const;
	bool ShouldDisableMotionMatchingBeyondFarDistance() const;
	const UProject_JCombatAnimProfile* GetCombatAnimProfile() const;
	float GetEffectiveCombatAimAlpha() const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback Chooser Table. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UChooserTable> MotionMatchingChooserTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback locomotion PSD. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UPoseSearchDatabase> DefaultPoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback idle PSD. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UPoseSearchDatabase> DefaultIdlePoseSearchDatabase = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Motion Matching")
	TObjectPtr<UPoseSearchDatabase> CurrentActivePoseSearchDatabase = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimThreadSafeData ThreadSafeData;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserGroundSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserVerticalSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserAccelerationRatio = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputSize = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputHeldTime = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputTurnAngle = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserLastFallSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserLandStartFallSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserHasMoveInput = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStartRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStopRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserSharpTurnRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserWantsSprint = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRemoteRunStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunStop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintStop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRemoteRunLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLocomotionRow = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseJumpStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseFallOff = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseFallLoop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseHeavyLandRow = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseStandLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseStandHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserLandWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserLandWasMoving = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStartWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStopWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsInAir = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsJumping = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsLanding = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsCombatMode = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsIdle = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsRemoteProxy = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	EProject_JGroundMotionMode ChooserGroundMotionMode = EProject_JGroundMotionMode::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Advanced|Optimization", AdvancedDisplay, meta = (ToolTip = "Skips animation-only data work on dedicated servers. Event replication still runs."))
	bool bSkipDedicatedServerAnimationDataUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback optimization setting used when no effective LocomotionProfile is assigned."))
	float HiddenRemoteUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Advanced|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Render visibility tolerance used before hidden remote update throttling is allowed."))
	float RecentlyRenderedTolerance = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float NearMotionMatchingDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float MidMotionMatchingDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float FarMotionMatchingDistance = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback update interval used when no effective LocomotionProfile is assigned."))
	float MidMotionMatchingUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback update interval used when no effective LocomotionProfile is assigned."))
	float FarMotionMatchingUpdateInterval = 0.083f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ToolTip = "Fallback optimization setting used when no effective LocomotionProfile is assigned."))
	bool bDisableMotionMatchingBeyondFarDistance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Movement", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback movement threshold used when no effective LocomotionProfile is assigned."))
	float GenericMoveInputSpeedThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Movement", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback sprint threshold used when no effective LocomotionProfile is assigned."))
	float SprintLocomotionSpeedThreshold = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimYaw = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimPitch = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StandingAimAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MovingAimAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SprintAimAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CombatAimAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides foot placement plant settings."))
	FFootPlacementPlantSettings FootPlacementPlantSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides stop foot placement plant settings."))
	FFootPlacementPlantSettings FootPlacementPlantSettingsStops;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides foot placement interpolation settings."))
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides stop foot placement interpolation settings."))
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsStops;

private:
	float HiddenRemoteUpdateAccumulator = 0.0f;
	float MotionMatchingUpdateAccumulator = 0.0f;
};
