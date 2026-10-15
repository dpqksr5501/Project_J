// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JRetargetBodyProfile.h"
#include "Animation/Project_JRetargetAnimInstanceProxy.h"
#include "Animation/Project_JHandContact.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UProject_JRetargetAnimInstance::BeginDestroy()
{
	if (CachedPresentationComp.IsValid())
	{
		CachedPresentationComp->UnregisterRetargetAnimInstance(this);
	}
	Super::BeginDestroy();
}

void UProject_JRetargetAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	// A reinitialized/rebound visual instance must not remain registered with
	// its previous presentation owner, including dedicated-server early exits.
	if (CachedPresentationComp.IsValid()) CachedPresentationComp->UnregisterRetargetAnimInstance(this);
	CachedPresentationComp.Reset(); CachedWeaponComponent.Reset();

	// Dedicated server: completely disable tick on follower visual mesh to avoid any wasted evaluation
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			if (USkeletalMeshComponent* OwningComp = GetOwningComponent())
			{
				OwningComp->SetComponentTickEnabled(false);
			}
			return;
		}
	}

	if (USkeletalMeshComponent* OwningComp = GetOwningComponent())
	{
		OwningComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	// Clear transient snapshots
	RightWristTarget = LeftWristTarget = FTransform::Identity;
	bRightContactTargetValid = bLeftContactTargetValid = false;
	bHasValidRightSnapshot = false;
	bHasValidLeftSnapshot = false;

	if (APawn* OwnerPawn = TryGetPawnOwner())
	{
		CachedPresentationComp = OwnerPawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
		if (CachedPresentationComp.IsValid())
		{
			CachedPresentationComp->RegisterRetargetAnimInstance(this);
		}
	}
}

void UProject_JRetargetAnimInstance::UpdateWeaponTarget(USceneComponent* InWeaponComponent, FName InSocketName)
{
	CachedWeaponComponent = InWeaponComponent;
	if (!InSocketName.IsNone())
	{
		PrimaryGripSocketName = InSocketName;
	}
}

void UProject_JRetargetAnimInstance::SetCombatMode(bool bInCombatMode)
{
	bIsCombatMode = bInCombatMode;
}

FProject_JHandGripCalibration UProject_JRetargetAnimInstance::GetHandGripCalibration() const
{
	check(IsInGameThread());
	if (const auto* Profile = GetResolvedHandGripProfile()) return Profile->Calibration;
	return Project_J::Animation::ResolveHandGripCalibration(Cast<ACharacter>(TryGetPawnOwner()), HandGripProfile);
}

FAnimInstanceProxy* UProject_JRetargetAnimInstance::CreateAnimInstanceProxy()
{ return new FProject_JRetargetAnimInstanceProxy(this); }
void UProject_JRetargetAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{ delete static_cast<FProject_JRetargetAnimInstanceProxy*>(Proxy); }

const UProject_JRetargetBodyProfile* UProject_JRetargetAnimInstance::GetRetargetBodyProfile() const
{
	check(IsInGameThread());
	const auto* Body = RetargetBodyProfile.Get();
	if (!Body)
	{
		// An explicit visual-body selection already owns precedence. Resolve the
		// pawn/archetype only for the fallback, without adding cached lifetime state.
		const auto* Player = Cast<AProject_JPlayerCharacter>(TryGetPawnOwner());
		const auto* CharacterProfile = Player ? Player->GetCharacterAnimProfile() : nullptr;
		Body = CharacterProfile ? CharacterProfile->RetargetBodyProfile.Get() : nullptr;
	}
	const auto* Mesh = GetSkelMeshComponent();
	return Body && Body->Family && Mesh && Mesh->GetSkeletalMeshAsset() &&
		Body->Family->TargetSkeleton == Mesh->GetSkeletalMeshAsset()->GetSkeleton() ? Body : nullptr;
}

const UProject_JHandGripProfile* UProject_JRetargetAnimInstance::GetResolvedHandGripProfile() const
{
	check(IsInGameThread());
	// Existing explicit graph overrides retain priority during gradual migration.
	if (HandGripProfile) return HandGripProfile;
	if (const auto* Body = GetRetargetBodyProfile()) if (Body->HandGrip) return Body->HandGrip;
	const auto* Player = Cast<AProject_JPlayerCharacter>(TryGetPawnOwner());
	const auto* Profile = Player ? Player->GetCharacterAnimProfile() : nullptr;
	return Profile ? Profile->HandGripProfile.Get() : nullptr;
}

Project_J::Animation::FResolvedHandContact UProject_JRetargetAnimInstance::GetBodyHandContact(bool bPrimary) const
{
	return GetBodyHandContact(bPrimary, GetHandGripCalibration());
}

Project_J::Animation::FResolvedHandContact UProject_JRetargetAnimInstance::GetBodyHandContact(
	bool bPrimary, const FProject_JHandGripCalibration& Calibration) const
{
	check(IsInGameThread());
	const auto* Mesh = GetSkelMeshComponent();
	if (!Mesh) return {};
	return Project_J::Animation::ResolveHandContact(*Mesh,
		bPrimary ? Calibration.PrimaryPalmSocketName : Calibration.SecondaryPalmSocketName,
		bPrimary ? Calibration.PrimaryArm.Hand : Calibration.SecondaryArm.Hand,
		Calibration.MissingPalmPolicy == EProject_JMissingPalmPolicy::LegacyWristOrigin);
}

void UProject_JRetargetAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RetargetBody_Snapshot);
	Super::NativeUpdateAnimation(DeltaSeconds);
	GripTargetSnapshotFrame = MAX_uint64;

	// Per-frame snapshot initialization to eliminate stale transforms
	bHasValidRightSnapshot = false;
	bPrimaryIKSuppressedSnapshot = false;
	bContactRecoverySnapshot = false;
	bHasValidLeftSnapshot = false;
	TargetRightAlphaSnapshot = 0.0f;
	TargetLeftAlphaSnapshot = 0.0f;
	SnapshotRightElbowTarget = FVector::ZeroVector;
	SnapshotLeftElbowTarget = FVector::ZeroVector;
	SnapshotLeftGripInPrimaryHandSpace = FTransform::Identity;
	SnapshotPrimaryHandBoneName = NAME_None;
	bHasPrimaryHandSpaceGripSnapshot = false;

	// Dedicated server early-out: visual IK and retargeting evaluation are client-only concerns.
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		return;
	}

	// 1. Synchronize animation quality tier from Leader Mesh policy
	if (const ACharacter* OwnerChar = Cast<ACharacter>(OwnerPawn))
	{
		if (const USkeletalMeshComponent* LeaderMesh = OwnerChar->GetMesh())
		{
			if (const UProject_JCharacterAnimInstance* LeaderAnim = Cast<UProject_JCharacterAnimInstance>(LeaderMesh->GetAnimInstance()))
			{
				const FProject_JAnimOptimizationPolicy& Policy = LeaderAnim->GetCurrentOptimizationPolicy();
				CurrentQualityTier = Policy.Tier;
				bEnableFollowerRetarget = Policy.bEnableFollowerRetarget;
				bTierAllowsHandIK = Policy.bEnableHandIK;
				bTierAllowsFootIK = Policy.bEnableFootIK;
				bTierAllowsRetargetIK = Policy.bEnableRetargetIK;
				RetargetIKLODThreshold = Policy.RetargetIKLODThreshold;
				RetargetLODThreshold = Policy.RetargetLODThreshold;
			}
		}
	}

	// Early out if tier forbids Follower Retarget / Hand IK or if character is off-screen/hidden
	if (!bEnableFollowerRetarget || !bTierAllowsHandIK || CurrentQualityTier == EProject_JAnimBudgetTier::Hidden)
	{
		return;
	}

	// 2. Synchronize combat state deterministically from local gameplay authority / ASC
	if (const AProject_JPlayerCharacter* PlayerChar = Cast<AProject_JPlayerCharacter>(OwnerPawn))
	{
		bIsCombatMode = PlayerChar->IsCombatModeActive();
	}
	else if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerPawn))
	{
		bIsCombatMode = ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	}

	USkeletalMeshComponent* OwningComp = GetOwningComponent();
	if (!OwningComp)
	{
		return;
	}
	SnapshotOwningCompWorldTransform = OwningComp->GetComponentTransform();
	GripTargetSnapshotFrame = GFrameCounter;

	// 3. Check Leader AnimInstance curves (supports authored high-precision Float Curves on attack montages)
	float LeaderLeftCurveValue = -1.0f;
	float LeaderRightCurveValue = -1.0f;
	if (const ACharacter* OwnerChar = Cast<ACharacter>(OwnerPawn))
	{
		if (const USkeletalMeshComponent* LeaderMesh = OwnerChar->GetMesh())
		{
			if (const UAnimInstance* LeaderAnim = LeaderMesh->GetAnimInstance())
			{
				if (!LeftHandIKCurveName.IsNone())
				{
					float Val = 0.0f;
					if (LeaderAnim->GetCurveValue(LeftHandIKCurveName, Val))
					{
						LeaderLeftCurveValue = FMath::Clamp(Val, 0.0f, 1.0f);
					}
				}
				if (!RightHandIKCurveName.IsNone())
				{
					float Val = 0.0f;
					if (LeaderAnim->GetCurveValue(RightHandIKCurveName, Val))
					{
						LeaderRightCurveValue = FMath::Clamp(Val, 0.0f, 1.0f);
					}
				}
			}
		}
	}

	// 4. Try consuming from UProject_JWeaponPresentationComponent if present
	if (!CachedPresentationComp.IsValid())
	{
		CachedPresentationComp = OwnerPawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
	}

	bool bResolvedFromPresentation = false;
	if (CachedPresentationComp.IsValid())
	{
		const FProject_JWeaponGripTargets GripTargets = CachedPresentationComp->GetWeaponGripTargetsForAnimation(DeltaSeconds);
		// Presentation owns availability, including an explicitly disabled secondary
		// contact or missing equipment. Legacy component tracking must not resurrect it.
		bResolvedFromPresentation = true;
		bContactRecoverySnapshot = GripTargets.bContactRecovery;
		if (GripTargets.bHasPrimaryGrip)
		{
			SnapshotRightGripWorldTransform = GripTargets.PrimaryGripWorldTransform;
			bHasValidRightSnapshot = true;
			bPrimaryIKSuppressedSnapshot = GripTargets.bPrimaryIKSuppressedByAttachment;
			// The visible hand already drives an attached weapon. An authored curve
			// cannot reintroduce a self-following primary-hand IK loop in this mode.
			const float PrimaryBaseAlpha = GripTargets.bPrimaryIKSuppressedByAttachment
				? 0.0f : ((!GripTargets.bContactRecovery && LeaderRightCurveValue >= 0.0f) ? LeaderRightCurveValue : GripTargets.PrimaryIKAlpha);
			TargetRightAlphaSnapshot = bIsCombatMode
				? (bEnableCombatGripIK ? PrimaryBaseAlpha : 0.0f)
				: PrimaryBaseAlpha;
			bResolvedFromPresentation = true;
		}
		if (GripTargets.bHasSecondaryGrip)
		{
			SnapshotLeftGripWorldTransform = GripTargets.SecondaryGripWorldTransform;
			bHasValidLeftSnapshot = true;
			const float SecondaryBaseAlpha = (!GripTargets.bContactRecovery && LeaderLeftCurveValue >= 0.0f)
				? LeaderLeftCurveValue : GripTargets.SecondaryIKAlpha;
			TargetLeftAlphaSnapshot = bIsCombatMode
				? (bEnableCombatGripIK ? SecondaryBaseAlpha : 0.0f)
				: SecondaryBaseAlpha;
			if (GripTargets.bHasPrimaryHandSpaceGrip)
			{
				SnapshotLeftGripInPrimaryHandSpace = GripTargets.SecondaryGripInPrimaryHandSpace;
				SnapshotPrimaryHandBoneName = GripTargets.PrimaryHandBoneName;
				bHasPrimaryHandSpaceGripSnapshot = true;
			}
			bResolvedFromPresentation = true;
		}
	}

	// 3. Fallback: Component tracking if not resolved via WeaponPresentationComponent
	if (!bResolvedFromPresentation)
	{
		// Auto-detect fallback only when enabled explicitly
		if (bAutoDetectWeaponIfNull && !CachedWeaponComponent.IsValid())
		{
			TArray<USceneComponent*> Components;
			OwnerPawn->GetComponents(Components);
			for (USceneComponent* Comp : Components)
			{
				if (Comp && Comp->DoesSocketExist(PrimaryGripSocketName))
				{
					CachedWeaponComponent = Comp;
					break;
				}
			}

			if (!CachedWeaponComponent.IsValid())
			{
				TArray<AActor*> AttachedActors;
				OwnerPawn->GetAttachedActors(AttachedActors);
				for (AActor* Attached : AttachedActors)
				{
					if (Attached)
					{
						TArray<USceneComponent*> AttachedComponents;
						Attached->GetComponents(AttachedComponents);
						for (USceneComponent* AttachedComp : AttachedComponents)
						{
							if (AttachedComp && AttachedComp->DoesSocketExist(PrimaryGripSocketName))
							{
								CachedWeaponComponent = AttachedComp;
								break;
							}
						}
						if (CachedWeaponComponent.IsValid())
						{
							break;
						}
					}
				}
			}
		}

		USceneComponent* WeaponComp = CachedWeaponComponent.Get();
		if (WeaponComp && WeaponComp->DoesSocketExist(PrimaryGripSocketName))
		{
			SnapshotRightGripWorldTransform = WeaponComp->GetSocketTransform(PrimaryGripSocketName, RTS_World);
			bHasValidRightSnapshot = true;
			// In sheathed state (weapon on back): Right hand alpha is 0.0 unless combat mode is active
			// In combat state (weapon drawn): Right hand aligns to grip if enabled (or driven by curve)
			const float PrimaryFallbackAlpha = (LeaderRightCurveValue >= 0.0f) ? LeaderRightCurveValue : 1.0f;
			TargetRightAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? PrimaryFallbackAlpha : 0.0f) : 0.0f;
		}
		else
		{
			bHasValidRightSnapshot = false;
			TargetRightAlphaSnapshot = 0.0f;
		}

		if (WeaponComp && WeaponComp->DoesSocketExist(SecondaryGripSocketName))
		{
			SnapshotLeftGripWorldTransform = WeaponComp->GetSocketTransform(SecondaryGripSocketName, RTS_World);
			bHasValidLeftSnapshot = true;
			// Secondary hand only follows if driven by curve; otherwise 0.0 in idle/run
			const float SecondaryFallbackAlpha = (LeaderLeftCurveValue >= 0.0f) ? LeaderLeftCurveValue : 0.0f;
			TargetLeftAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? SecondaryFallbackAlpha : 0.0f) : 0.0f;
		}
		else
		{
			bHasValidLeftSnapshot = false;
			TargetLeftAlphaSnapshot = 0.0f;
		}
	}

	// Resolve authored body anchors once per animation update, on the game thread.
	// The shared value conversion has exactly one owner. Solver inputs are WRIST
	// targets; neither a graph node nor a downstream rig should convert them again.
	{
		const FProject_JHandGripCalibration Calibration = GetHandGripCalibration();
		auto ResolveWristTarget = [this, OwningComp, &Calibration, PrimaryHand = SnapshotPrimaryHandBoneName](bool bPrimary,
			const FTransform& BodyOffset, FTransform& InOutGripWorld, FTransform* InOutHandSpace)
		{
			const Project_J::Animation::FResolvedHandContact Contact =
				GetBodyHandContact(bPrimary, Calibration);
			if (!Contact.IsValid()) { return false; }
			const FVector WristScale = Contact.Hand.IsNone() ? OwningComp->GetComponentScale()
				: OwningComp->GetBoneTransform(Contact.Hand, RTS_World).GetScale3D();
			FTransform Wrist;
			if (!Project_J::Animation::MakeWristContactTarget(InOutGripWorld, Contact.PalmInHand, BodyOffset, Wrist, WristScale)) { return false; }
			InOutGripWorld = Wrist;
			if (InOutHandSpace)
			{
				const FVector PrimaryScale = PrimaryHand.IsNone() ? OwningComp->GetComponentScale()
					: OwningComp->GetBoneTransform(PrimaryHand, RTS_World).GetScale3D();
				if (PrimaryScale.ContainsNaN() || PrimaryScale.GetAbs().GetMin() <= UE_KINDA_SMALL_NUMBER ||
					!Project_J::Animation::MakeWristContactTarget(*InOutHandSpace, Contact.PalmInHand, BodyOffset, Wrist, WristScale / PrimaryScale)) { return false; }
				*InOutHandSpace = Wrist;
			}
			return true;
		};
		if (bHasValidRightSnapshot)
		{
			bHasValidRightSnapshot = ResolveWristTarget(true,
				Calibration.PrimaryHandOffset, SnapshotRightGripWorldTransform, nullptr);
		}
		if (bHasValidLeftSnapshot)
		{
			bHasValidLeftSnapshot = ResolveWristTarget(false,
				Calibration.SecondaryHandOffset, SnapshotLeftGripWorldTransform,
				bHasPrimaryHandSpaceGripSnapshot ? &SnapshotLeftGripInPrimaryHandSpace : nullptr);
		}
		SnapshotRightElbowTarget = Calibration.PrimaryElbowTarget;
		SnapshotLeftElbowTarget = Calibration.SecondaryElbowTarget;
	}
}

void UProject_JRetargetAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_RetargetBody_ContactMath);
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	// Worker-thread evaluation: purely math on captured snapshots
	if (bHasValidRightSnapshot)
	{
		const FTransform LocalGrip = SnapshotRightGripWorldTransform.GetRelativeTransform(SnapshotOwningCompWorldTransform);
		RightGripLocation = LocalGrip.GetLocation();
		RightGripRotation = LocalGrip.Rotator();
	}
	else
	{
		RightGripLocation = FVector::ZeroVector;
		RightGripRotation = FRotator::ZeroRotator;
	}
	RightWristTarget = FTransform(RightGripRotation, RightGripLocation);
	bRightContactTargetValid = bHasValidRightSnapshot;

	if (bHasValidLeftSnapshot)
	{
		const FTransform LocalGrip = SnapshotLeftGripWorldTransform.GetRelativeTransform(SnapshotOwningCompWorldTransform);
		LeftGripLocation = LocalGrip.GetLocation();
		LeftGripRotation = LocalGrip.Rotator();
	}
	else
	{
		LeftGripLocation = FVector::ZeroVector;
		LeftGripRotation = FRotator::ZeroRotator;
	}
	LeftWristTarget = FTransform(LeftGripRotation, LeftGripLocation);
	bLeftContactTargetValid = bHasValidLeftSnapshot;
	bUsePrimaryHandSpaceGrip = bHasValidLeftSnapshot && bHasPrimaryHandSpaceGripSnapshot;
	LeftGripInPrimaryHandSpace = bUsePrimaryHandSpaceGrip ? SnapshotLeftGripInPrimaryHandSpace : FTransform::Identity;
	PrimaryHandBoneName = bUsePrimaryHandSpaceGrip ? SnapshotPrimaryHandBoneName : NAME_None;
	RightElbowTarget = SnapshotRightElbowTarget;
	LeftElbowTarget = SnapshotLeftElbowTarget;

	// Smoothly interpolate IK alphas to eliminate snapping between states
	RightGripAlpha = bHasValidRightSnapshot && !bPrimaryIKSuppressedSnapshot
		? (bContactRecoverySnapshot ? TargetRightAlphaSnapshot
			: FMath::FInterpTo(RightGripAlpha, TargetRightAlphaSnapshot, DeltaSeconds, GripInterpSpeed)) : 0.0f;
	LeftGripAlpha = bHasValidLeftSnapshot
		? (bContactRecoverySnapshot ? TargetLeftAlphaSnapshot
			: FMath::FInterpTo(LeftGripAlpha, TargetLeftAlphaSnapshot, DeltaSeconds, GripInterpSpeed)) : 0.0f;
	GripIKAlpha = RightGripAlpha;
}

void UProject_JRetargetAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	GripPoseEvaluationFrame = GFrameCounter;
}
