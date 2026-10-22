#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Animation/Project_JRiderAnimationProfile.h"
#include "Project_JPlayerCharacter.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"

UProject_JCharacterAnimInstance::UProject_JCharacterAnimInstance() { bUseMultiThreadedAnimationUpdate=true; }
void UProject_JCharacterAnimInstance::NativeInitializeAnimation()
{
 ReleaseFollowerTickOverride();
 ThreadSafeData={};
 bHasStateControllerFootContactCurves=bHasStateControllerFootPhaseHistory=false;
 CachedStateControllerLeftFootContact=CachedStateControllerRightFootContact=0.f;
 StateControllerFootPhaseHistory=EProject_JStateControllerFoot::None;
 Super::NativeInitializeAnimation();
}
void UProject_JCharacterAnimInstance::NativeUninitializeAnimation()
{
 ReleaseFollowerTickOverride();
 Super::NativeUninitializeAnimation();
}
void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
 Super::NativeUpdateAnimation(DeltaSeconds);
 if (NeedsOwnerReferenceRefresh()) CacheOwnerReferences();
 if (IsPrimaryMeshAnimInstance()) ApplyOptimizationPolicy(BuildOptimizationPolicy());
 FProject_JAnimThreadSafeData Data;
 Data.DeltaTime=DeltaSeconds;
 if (OwningCharacter)
 {
  Data.Movement.Velocity=OwningCharacter->GetVelocity();
  Data.Movement.GroundSpeed=Data.Movement.Velocity.Size2D();
  const auto Local=OwningCharacter->GetActorQuat().UnrotateVector(Data.Movement.Velocity);
  Data.Movement.RelativeVelocityDirection=Data.Movement.GroundSpeed>UE_SMALL_NUMBER ? FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X)):0.f;
  FillPlayerThreadSafeData(Data);
  FillMountThreadSafeData(Data);
  FillMontageThreadSafeData(Data);
 }
 ThreadSafeData=Data;
 GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().QueueGameThreadData(Data);
}
void UProject_JCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	// The engine owns update/evaluation ordering for both primary and linked
	// instances. Copy only immutable snapshot values here: state-controller values
	// can change later during graph traversal and must keep their existing getters.
	const auto& Snapshot = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	GraphAimYaw = Snapshot.Aim.AimYaw;
	GraphAimPitch = Snapshot.Aim.AimPitch;
	GraphAimOffsetAlpha = Snapshot.Aim.AimOffsetAlpha;
	GraphCombatSpeed = Snapshot.Movement.GroundSpeed;
	GraphCombatDirection = Snapshot.Movement.RelativeVelocityDirection;
	// This callback is part of UE's thread-safe update phase, but the Task Graph
	// may execute that phase as a foreground task. Record UE's runtime state so
	// Insights distinguishes actual parallel evaluation from foreground work.
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(IsInGameThread() ? TEXT("ProjectJ_AnimThreadSafeUpdate_GameThread") : TEXT("ProjectJ_AnimThreadSafeUpdate_Worker"));
	if (IsRunningParallelEvaluation())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate_ParallelEvaluation);
		Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate_Foreground);
		Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	}
}
void UProject_JCharacterAnimInstance::NativePostEvaluateAnimation()
{
	// Post-evaluate is intentionally measured separately from NativeUpdate and
	// NativeThreadSafeUpdate. Its thread lane in Insights distinguishes the
	// completion-side contact-curve work from worker-side graph evaluation.
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativePostEvaluate);
	Super::NativePostEvaluateAnimation();

	if (!IsPrimaryMeshAnimInstance())
	{
		return;
	}
	float LeftContact = 0.0f;
	float RightContact = 0.0f;
	const bool bHasLeftContact = GetCurveValue(TEXT("contact_l"), LeftContact);
	const bool bHasRightContact = GetCurveValue(TEXT("contact_r"), RightContact);
	bHasStateControllerFootContactCurves = bHasLeftContact && bHasRightContact;
	if (bHasStateControllerFootContactCurves)
	{
		CachedStateControllerLeftFootContact = FMath::Clamp(LeftContact, 0.0f, 1.0f);
		CachedStateControllerRightFootContact = FMath::Clamp(RightContact, 0.0f, 1.0f);
		const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
		if (FMath::Abs(ContactDelta) >= StateControllerFootContactDifferenceThreshold)
		{
			StateControllerFootPhaseHistory = ContactDelta < 0.0f
				? EProject_JStateControllerFoot::Left
				: EProject_JStateControllerFoot::Right;
			bHasStateControllerFootPhaseHistory = true;
		}
	}
	else
	{
		CachedStateControllerLeftFootContact = 0.0f;
		CachedStateControllerRightFootContact = 0.0f;
	}
}
FAnimInstanceProxy* UProject_JCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FProject_JCharacterAnimInstanceProxy(this);
}
void UProject_JCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
bool UProject_JCharacterAnimInstance::IsPrimaryMeshAnimInstance() const
{
	return OwningCharacter &&
		OwningCharacter->GetMesh() &&
		OwningCharacter->GetMesh()->GetAnimInstance() == this;
}
const UProject_JLocomotionProfile* UProject_JCharacterAnimInstance::GetLocomotionProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionProfile() : nullptr;
}
EProject_JStateControllerFoot UProject_JCharacterAnimInstance::ResolveStateControllerFootFromContactCurves(
	const bool bAllowPhaseHistoryFallback,
	EProject_JStateControllerFootSelectionReason& OutReason) const
{
	auto UsePhaseHistoryOrDefault = [this, bAllowPhaseHistoryFallback, &OutReason]()
	{
		if (bAllowPhaseHistoryFallback && bHasStateControllerFootPhaseHistory)
		{
			OutReason = EProject_JStateControllerFootSelectionReason::PhaseHistoryFallback;
			return StateControllerFootPhaseHistory;
		}

		OutReason = EProject_JStateControllerFootSelectionReason::DefaultFootFallback;
		return StateControllerNoPhaseFootFallback;
	};

	if (!bHasStateControllerFootContactCurves)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::MissingContactCurve;
		return UsePhaseHistoryOrDefault();
	}

	const bool bLeftUnplanted = CachedStateControllerLeftFootContact < StateControllerFootContactDifferenceThreshold;
	const bool bRightUnplanted = CachedStateControllerRightFootContact < StateControllerFootContactDifferenceThreshold;
	if (bLeftUnplanted && bRightUnplanted)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::BothFeetUnplanted;
		return UsePhaseHistoryOrDefault();
	}

	const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
	if (FMath::Abs(ContactDelta) < StateControllerFootContactDifferenceThreshold)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::ContactsTooSimilar;
		return UsePhaseHistoryOrDefault();
	}

	// A _Lfoot/_Rfoot one-shot begins by moving that foot. Pick the foot with
	// lower contact (the current swing/airborne foot) to continue the stride.
	if (ContactDelta < 0.0f)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::LeftFootLowerContact;
		return EProject_JStateControllerFoot::Left;
	}

	OutReason = EProject_JStateControllerFootSelectionReason::RightFootLowerContact;
	return EProject_JStateControllerFoot::Right;
}
EProject_JAnimationLocomotionMode UProject_JCharacterAnimInstance::GetThreadSafeLocomotionMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionMode;
}
float UProject_JCharacterAnimInstance::GetThreadSafeFullBodyMontageWeight() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Montage.FullBodyMontageWeight;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeIsCombatMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.bIsCombatMode;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeUsesFullBodyCombatLocomotion() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.PresentationMode ==
		EProject_JCombatAnimationPresentationMode::FullBodyLocomotion;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeIsMounted() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsMounted;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsFlying() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsFlying;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsGliding() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsGliding;
}
float UProject_JCharacterAnimInstance::GetThreadSafeMountedSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.Speed;
}
float UProject_JCharacterAnimInstance::GetThreadSafeMountedVerticalSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.VerticalSpeed;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeHasMountedHandIKTargets() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bHasHandIKTargets;
}
FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedLeftHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.LeftHandTargetComponentSpace;
}
FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedRightHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.RightHandTargetComponentSpace;
}
FGameplayTagContainer UProject_JCharacterAnimInstance::GetThreadSafeMountedAnimationTags() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.AnimationTags;
}
bool UProject_JCharacterAnimInstance::GetThreadSafeMountedHasAnimationTag(FGameplayTag Tag) const
{
	return Tag.IsValid() &&
		GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.AnimationTags.HasTag(Tag);
}
float UProject_JCharacterAnimInstance::GetThreadSafeMountedTransitionBlendTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.TransitionBlendTime;
}
bool UProject_JCharacterAnimInstance::FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return false;
	}

	Data.Combat.bIsCombatMode = OwningPlayerCharacter->IsCombatModeActive();
	Data.Combat.bIsAttacking = OwningPlayerCharacter->IsAttacking();
	Data.Combat.bIsDodging = OwningPlayerCharacter->IsDodging();
	Data.Combat.bIsHitReacting = OwningPlayerCharacter->IsHitReacting();
	Data.Combat.bIsPlayingCombatIntro = OwningPlayerCharacter->IsCombatIntroPlaying();
	Data.Combat.bIsPlayingCombatOutro = OwningPlayerCharacter->IsCombatOutroPlaying();
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter->GetWeaponAnimProfile())
	{
		Data.Combat.PresentationMode = WeaponAnimProfile->CombatPresentationMode;
	}

	// Combat layers and the master consume the same completed movement command.
	const auto* Mover = OwningPlayerCharacter->GetMover();
	const auto* Input = Mover ? Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>() : nullptr;
	const FRotator AimRotation = Input ? Input->ControlRotation : OwningCharacter->GetBaseAimRotation();
	const FRotator ActorRotation = OwningCharacter->GetActorRotation();
	Data.Aim.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, AimRotation.Yaw), -MaxAimYaw, MaxAimYaw);
	Data.Aim.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -MaxAimPitch, MaxAimPitch);
	const auto* CombatProfile=OwningPlayerCharacter->GetCombatAnimProfile();
	Data.Aim.AimOffsetAlpha=Data.Combat.bIsCombatMode ? (CombatProfile?CombatProfile->CombatAimAlpha:CombatAimAlpha) : 0.f;
	return true;
}
void UProject_JCharacterAnimInstance::FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return;
	}

	Data.LocomotionMode = OwningPlayerCharacter->GetAnimationLocomotionMode();

	const UProject_JMountComponent* MountComponent = OwningPlayerCharacter->GetMountComponent();
	const AProject_JMountCharacter* MountedMount = MountComponent ? MountComponent->GetMountedMount() : nullptr;
	if (!MountedMount)
	{
		return;
	}

	// This snapshot describes the physical rider/mount relation. It must not
	// depend on the broader presentation context: a future vehicle context may
	// still use the same attachment and rider hand-target contract.
	Data.Mount.bIsMounted = true;
	if (const UProject_JRiderAnimationProfile* RiderProfile = MountedMount->GetRiderAnimationProfile())
	{
		Data.Mount.AnimationTags = RiderProfile->AnimationTags;
		Data.Mount.TransitionBlendTime = RiderProfile->TransitionBlendTime;
	}
	const FVector MountVelocity = MountedMount->GetVelocity();
	Data.Mount.Speed = MountVelocity.Size2D();
	Data.Mount.VerticalSpeed = MountVelocity.Z;

	if (const AProject_JFlyingMountCharacter* FlyingMount = Cast<AProject_JFlyingMountCharacter>(MountedMount))
	{
		Data.Mount.bIsFlying = FlyingMount->IsFlyingMount();
		Data.Mount.bIsGliding = FlyingMount->IsGliding();
	}

	USkeletalMeshComponent* RiderMesh = OwningPlayerCharacter->GetMesh();
	FVector LeftHandTargetWorld;
	FVector RightHandTargetWorld;
	if (RiderMesh && MountedMount->GetRiderHandIKTargetsWorld(LeftHandTargetWorld, RightHandTargetWorld))
	{
		const FTransform RiderMeshTransform = RiderMesh->GetComponentTransform();
		Data.Mount.LeftHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(LeftHandTargetWorld);
		Data.Mount.RightHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(RightHandTargetWorld);
		Data.Mount.bHasHandIKTargets = true;
	}
}

void UProject_JCharacterAnimInstance::FillMontageThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
 check(IsInGameThread());
 Data.bIsLocallyControlled=IsLocallyControlledCharacter();
 Data.Montage.FullBodyMontageWeight=Data.LocomotionMode==EProject_JAnimationLocomotionMode::OnFoot
  ? FMath::Clamp(GetSlotMontageGlobalWeight(TEXT("DefaultSlot")),0.f,1.f):0.f;
}
FProject_JAnimPlaybackSnapshot UProject_JCharacterAnimInstance::GetPlaybackSnapshot() const
{
 return GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().GetLatestPostSelection();
}
FString UProject_JCharacterAnimInstance::GetAnimationDebugSummary() const
{
 const auto Playback=GetPlaybackSnapshot();
 return FString::Printf(TEXT("PlayerLocomotion Playback=%s Time=%.3f/%.3f Rate=%.3f Node=%d Weight=%.3f Frame=%llu Speed=%.1f Air=%d Montage=%.3f"),
  *Playback.SelectedAnimation.ToString(),Playback.SelectedAnimationTime,Playback.SelectedAnimationLength,
  Playback.WantedPlayRate,Playback.ProducerNodeIndex,Playback.CachedNodeWeight,Playback.CaptureFrame,
  ThreadSafeData.Movement.GroundSpeed,ThreadSafeData.Air.bIsInAir,ThreadSafeData.Montage.FullBodyMontageWeight);
}
bool UProject_JCharacterAnimInstance::NeedsFullRateVisualUpdate() const
{
 check(IsInGameThread());
 const auto& Movement=ThreadSafeData.Movement;
 const bool bPivot=ThreadSafeData.Input.bHasMoveInput && Movement.Velocity.SizeSquared2D()>FMath::Square(50.f) &&
  !Movement.Acceleration.IsNearlyZero() && FVector::DotProduct(Movement.Velocity.GetSafeNormal2D(),Movement.Acceleration.GetSafeNormal2D())<.5f;
 return ThreadSafeData.Air.bIsInAir || ThreadSafeData.Combat.bIsAttacking || ThreadSafeData.Combat.bIsDodging ||
  ThreadSafeData.Combat.bIsHitReacting || ThreadSafeData.Montage.FullBodyMontageWeight>ZERO_ANIMWEIGHT_THRESH ||
  bPivot || FMath::Abs(Movement.FutureTrajectoryTurnAngle)>=60.f;
}
