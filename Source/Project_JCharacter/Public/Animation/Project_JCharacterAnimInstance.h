#pragma once
#include "CoreMinimal.h"
#include "Animation/Project_JCharacterAnimInstanceBase.h"
#include "Animation/Project_JAnimationSnapshot.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Project_JCharacterAnimInstance.generated.h"

class UProject_JLocomotionProfile;

/** Common combat, rider and quality snapshot. GASP owns locomotion selection and playback. */
UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstance : public UProject_JCharacterAnimInstanceBase
{
 GENERATED_BODY()
public:
 UProject_JCharacterAnimInstance();
 virtual void NativeInitializeAnimation() override;
 virtual void NativeUninitializeAnimation() override;
 virtual void NativeUpdateAnimation(float DeltaSeconds) override;
 virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
 virtual void NativePostEvaluateAnimation() override;
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
 FProject_JAnimOptimizationPolicy BuildOptimizationPolicy() const;
 void ApplyOptimizationPolicy(const FProject_JAnimOptimizationPolicy& NewPolicy);
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 EProject_JAnimationLocomotionMode GetThreadSafeLocomotionMode() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 float GetThreadSafeFullBodyMontageWeight() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeIsCombatMode() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeUsesFullBodyCombatLocomotion() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeIsMounted() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeMountedIsFlying() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeMountedIsGliding() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 float GetThreadSafeMountedSpeed() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 float GetThreadSafeMountedVerticalSpeed() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeHasMountedHandIKTargets() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 FVector GetThreadSafeMountedLeftHandTargetComponentSpace() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 FVector GetThreadSafeMountedRightHandTargetComponentSpace() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 FGameplayTagContainer GetThreadSafeMountedAnimationTags() const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 bool GetThreadSafeMountedHasAnimationTag(FGameplayTag Tag) const;
 UFUNCTION(BlueprintPure, Category="Animation|Snapshot", meta=(BlueprintThreadSafe))
 float GetThreadSafeMountedTransitionBlendTime() const;

 FString GetAnimationDebugSummary() const;
 FProject_JAnimPlaybackSnapshot GetPlaybackSnapshot() const;
 bool NeedsFullRateVisualUpdate() const;
 const FProject_JAnimOptimizationPolicy& GetCurrentOptimizationPolicy() const { return CurrentOptimizationPolicy; }
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Quality")
 FProject_JAnimOptimizationPolicy CurrentOptimizationPolicy;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Graph Snapshot")
 float GraphAimYaw=0.f;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Graph Snapshot")
 float GraphAimPitch=0.f;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Graph Snapshot")
 float GraphAimOffsetAlpha=0.f;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Graph Snapshot")
 float GraphCombatSpeed=0.f;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Animation|Graph Snapshot")
 float GraphCombatDirection=0.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Presentation", meta=(ClampMin="0.0"))
 float MaxAimYaw=90.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Presentation", meta=(ClampMin="0.0"))
 float MaxAimPitch=60.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Presentation", meta=(ClampMin="0.0"))
 float CombatAimAlpha=1.f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Presentation", meta=(ClampMin="0.0"))
 float RecentlyRenderedTolerance=.25f;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Presentation", meta=(ClampMin="0.0"))
 float StateControllerFootContactDifferenceThreshold=.20f;

 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation|Air")
 EProject_JStateControllerFoot StateControllerNoPhaseFootFallback=EProject_JStateControllerFoot::Left;
protected:
 FProject_JAnimThreadSafeData ThreadSafeData;
 void FillMontageThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
 const UProject_JLocomotionProfile* GetLocomotionProfile() const;
 FProject_JAnimationBudgetSettings GetEffectiveAnimationBudgetSettings() const;
 bool IsPrimaryMeshAnimInstance() const;
 bool FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
 void FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
 EProject_JStateControllerFoot ResolveStateControllerFootFromContactCurves(
	const bool bAllowPhaseHistoryFallback,
	EProject_JStateControllerFootSelectionReason& OutReason) const;

private:
 friend class UProject_JAnimationUpdateCoordinatorComponent;
 friend class UProject_JCharacterAnimationBudgetSubsystem;
 friend class FProjectJPlayerLocomotionSnapshotBoundaryTest;
 friend class FProjectJAnimationProxyLifetimeTest;
 friend class FProjectJPlaybackObservationLifetimeTest;
 void ReleaseFollowerTickOverride(bool bRestoreTick=true);
 TWeakObjectPtr<USkeletalMeshComponent> FallbackFollowerTickMesh;
 bool bSavedFollowerTick=true;
 bool bAppliedFollowerRetarget=true;
 bool bHasStateControllerFootContactCurves=false;
 bool bHasStateControllerFootPhaseHistory=false;
 float CachedStateControllerLeftFootContact=0.f;
 float CachedStateControllerRightFootContact=0.f;
 EProject_JStateControllerFoot StateControllerFootPhaseHistory=EProject_JStateControllerFoot::None;
};
