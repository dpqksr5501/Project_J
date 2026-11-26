#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatQueryTypes.generated.h"

/** Generalized combat query shapes extending existing 64-key melee trajectory (BP04 / A09). */
UENUM(BlueprintType)
enum class EProject_JHitQueryKind : uint8
{
	MeleeTrajectory,
	AoECircle,
	AoECone,
	AoERectangle,
	AoEDonut,
	ProjectileSweep,
	MultiHitChannel,
	MultiPartBoss
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JHitQuerySpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	EProject_JHitQueryKind Kind = EProject_JHitQueryKind::AoECircle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	FVector ForwardDir = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "0.0"))
	float OuterRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "0.0"))
	float InnerRadius = 0.0f; // Used by AoEDonut

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float HalfAngleDeg = 45.0f; // Used by AoECone

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	FVector HalfExtents = FVector(200.0f, 100.0f, 150.0f); // Used by AoERectangle

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxTargets = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "0.0"))
	float PerTargetHitCooldown = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	bool bRequireLineOfSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query")
	bool bAllowSSR = false;

	/** Existing shape queries allow contact with a small body margin. Ground warnings use center membership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Query", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float TargetPaddingCm = 45.0f;
};

/** Server-clamped Motion Warping attack target (prevents client warp-distance cheats). */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAttackWarpTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Warp")
	FVector RequestedLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Warp")
	FRotator RequestedRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Warp", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float MaxAllowedWarpDistanceCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Warp", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxAllowedYawDeltaDeg = 120.0f;
};

/** Distance-based Motion Matching LOD for remote players (BP04). NPCs remain permanently non-MM. */
UENUM(BlueprintType)
enum class EProject_JRemoteLocomotionLOD : uint8
{
	LocalFullMM,
	NearCompactMM,
	MidBlendSpace,
	FarSimple1D,
	Culled
};

/** Crowd VFX ownership tier preserving 100% MandatoryWarning visibility in 200-player boss fights. */
UENUM(BlueprintType)
enum class EProject_JCombatFXOwnerTier : uint8
{
	MandatoryBossWarning,
	LocalPlayer,
	PartyMember,
	OtherPlayer
};

class UAbilitySystemComponent;

namespace Project_J::Combat
{
	PROJECT_JCHARACTER_API bool CanAffectCombatTarget(const AActor* Source, const AActor* Target, const UAbilitySystemComponent* TargetASC);
	inline double GetCircleHalfHeight(double Radius) { return FMath::Max(150.0, Radius); }
	/** Pure geometric membership test for AoECircle / AoECone / AoERectangle / AoEDonut. */
	inline bool IsPointInsideAoESpec(const FProject_JHitQuerySpec& Spec, const FVector& TargetPoint, float TargetRadius = 0.0f)
	{
		if (TargetPoint.ContainsNaN() || Spec.Origin.ContainsNaN())
		{
			return false;
		}
		const FVector Delta = TargetPoint - Spec.Origin;
		const double Dist2D = Delta.Size2D();
		const double EffectiveOuter = static_cast<double>(Spec.OuterRadius) + static_cast<double>(FMath::Max(0.0f, TargetRadius));

		switch (Spec.Kind)
		{
		case EProject_JHitQueryKind::AoECircle:
			return Dist2D <= EffectiveOuter && FMath::Abs(Delta.Z) <= GetCircleHalfHeight(EffectiveOuter);

		case EProject_JHitQueryKind::AoEDonut:
		{
			const double EffectiveInner = FMath::Max(0.0, static_cast<double>(Spec.InnerRadius) - static_cast<double>(FMath::Max(0.0f, TargetRadius)));
			return Dist2D >= EffectiveInner && Dist2D <= EffectiveOuter && FMath::Abs(Delta.Z) <= 250.0;
		}

		case EProject_JHitQueryKind::AoECone:
		{
			if (Dist2D > EffectiveOuter || FMath::Abs(Delta.Z) > 250.0)
			{
				return false;
			}
			if (Dist2D <= UE_KINDA_SMALL_NUMBER)
			{
				return true;
			}
			const FVector Fwd2D = FVector(Spec.ForwardDir.X, Spec.ForwardDir.Y, 0.0).GetSafeNormal();
			const FVector Dir2D = FVector(Delta.X, Delta.Y, 0.0) / Dist2D;
			const double CosHalf = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Spec.HalfAngleDeg, 1.0f, 180.0f)));
			return FVector::DotProduct(Fwd2D, Dir2D) >= CosHalf;
		}

		case EProject_JHitQueryKind::AoERectangle:
		{
			const FVector Fwd2D = FVector(Spec.ForwardDir.X, Spec.ForwardDir.Y, 0.0).GetSafeNormal();
			const FVector Right2D(-Fwd2D.Y, Fwd2D.X, 0.0);
			const double LocalX = FVector::DotProduct(Delta, Fwd2D);
			const double LocalY = FVector::DotProduct(Delta, Right2D);
			const double R = static_cast<double>(FMath::Max(0.0f, TargetRadius));
			return FMath::Abs(LocalX) <= Spec.HalfExtents.X + R
				&& FMath::Abs(LocalY) <= Spec.HalfExtents.Y + R
				&& FMath::Abs(Delta.Z) <= Spec.HalfExtents.Z + R;
		}

		default:
			return false;
		}
	}

	/** Clamps a client-requested attack warp transform against server authority distance and yaw bounds. */
	inline FTransform ClampAttackWarpTarget(
		const FTransform& AttackerTransform,
		const FProject_JAttackWarpTarget& Request)
	{
		const FVector Origin = AttackerTransform.GetLocation();
		FVector Delta = Request.RequestedLocation - Origin;
		if (Delta.ContainsNaN())
		{
			Delta = FVector::ZeroVector;
		}
		Delta = Delta.GetClampedToMaxSize(FMath::Max(0.0f, Request.MaxAllowedWarpDistanceCm));

		const FRotator CurrentRot = AttackerTransform.Rotator();
		const float YawDiff = FRotator::NormalizeAxis(Request.RequestedRotation.Yaw - CurrentRot.Yaw);
		const float ClampedYaw = CurrentRot.Yaw + FMath::Clamp(YawDiff, -Request.MaxAllowedYawDeltaDeg, Request.MaxAllowedYawDeltaDeg);
		return FTransform(FRotator(0.0f, ClampedYaw, 0.0f), Origin + Delta);
	}

	/** Selects remote player locomotion Motion Matching LOD tier by distance (cm). */
	inline EProject_JRemoteLocomotionLOD ResolveRemoteLocomotionLOD(bool bIsLocalPlayer, double DistanceCm)
	{
		if (bIsLocalPlayer) return EProject_JRemoteLocomotionLOD::LocalFullMM;
		if (DistanceCm < 2000.0) return EProject_JRemoteLocomotionLOD::NearCompactMM;
		if (DistanceCm < 5000.0) return EProject_JRemoteLocomotionLOD::MidBlendSpace;
		if (DistanceCm < 10000.0) return EProject_JRemoteLocomotionLOD::FarSimple1D;
		return EProject_JRemoteLocomotionLOD::Culled;
	}

	/** Evaluates whether a combat VFX should spawn under crowd budget pressure. */
	inline bool AdmitCombatFX(EProject_JCombatFXOwnerTier Tier, int32 ActiveTierCount, double DistanceCm, bool bBossFightActive)
	{
		switch (Tier)
		{
		case EProject_JCombatFXOwnerTier::MandatoryBossWarning:
			return true; // Never culled (153/154 contract).
		case EProject_JCombatFXOwnerTier::LocalPlayer:
			return ActiveTierCount < 32;
		case EProject_JCombatFXOwnerTier::PartyMember:
			return DistanceCm <= 3000.0 && ActiveTierCount < 24;
		case EProject_JCombatFXOwnerTier::OtherPlayer:
		default:
			return DistanceCm <= 2000.0 && ActiveTierCount < (bBossFightActive ? 4 : 16);
		}
	}
}
