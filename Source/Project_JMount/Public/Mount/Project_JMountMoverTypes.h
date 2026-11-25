#pragma once

#include "CoreMinimal.h"
#include "MoverDataModelTypes.h"
#include "Mount/Project_JMountTypes.h"
#include "Project_JMountMoverTypes.generated.h"

/**
 * Network-predicted input stream for Mover-based ground & flying mounts (BP03).
 * Replaces ad-hoc reliable state RPCs (e.g. ServerSetGliding) with rollback-safe simulation input.
 */
USTRUCT(BlueprintType)
struct PROJECT_JMOUNT_API FProject_JMountMoverInputs : public FMoverDataStructBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	FVector2D ThrottleTurn = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	float PitchInput = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	bool bBoostHeld = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	bool bGlideHeld = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	bool bTakeOffPressed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	bool bLandPressed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount|Mover")
	uint8 FlightActionSeq = 0;

	/** Optional local speed reduction, encoded in the predicted command. */
	uint8 FlightStreamingSpeedScale = MAX_uint8;

	float GetFlightStreamingSpeedScale() const { return FMath::Max(0.1f, FlightStreamingSpeedScale / 255.0f); }

	virtual FMoverDataStructBase* Clone() const override { return new FProject_JMountMoverInputs(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar << ThrottleTurn << PitchInput;
		uint8 Flags = (bBoostHeld ? 1 : 0) | (bGlideHeld ? 2 : 0) | (bTakeOffPressed ? 4 : 0) | (bLandPressed ? 8 : 0);
		Ar << Flags << FlightActionSeq << FlightStreamingSpeedScale;
		if (Ar.IsLoading())
		{
			bBoostHeld = (Flags & 1) != 0;
			bGlideHeld = (Flags & 2) != 0;
			bTakeOffPressed = (Flags & 4) != 0;
			bLandPressed = (Flags & 8) != 0;
		}
		bOutSuccess = true;
		return true;
	}
	virtual void ToString(FAnsiStringBuilderBase& Out) const override
	{
		Super::ToString(Out);
		Out.Appendf("Throttle=%.2f Turn=%.2f Pitch=%.2f Glide=%d Boost=%d",
			ThrottleTurn.X, ThrottleTurn.Y, PitchInput, bGlideHeld ? 1 : 0, bBoostHeld ? 1 : 0);
	}
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		const auto* Other = static_cast<const FProject_JMountMoverInputs*>(&AuthorityState);
		return !ThrottleTurn.Equals(Other->ThrottleTurn, 0.05) || !FMath::IsNearlyEqual(PitchInput, Other->PitchInput, 0.05f)
			|| bBoostHeld != Other->bBoostHeld || bGlideHeld != Other->bGlideHeld
			|| bTakeOffPressed != Other->bTakeOffPressed || bLandPressed != Other->bLandPressed
			|| FlightActionSeq != Other->FlightActionSeq || FlightStreamingSpeedScale != Other->FlightStreamingSpeedScale;
	}
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override
	{
		const auto* A = static_cast<const FProject_JMountMoverInputs*>(&From);
		const auto* B = static_cast<const FProject_JMountMoverInputs*>(&To);
		ThrottleTurn = FMath::Lerp(A->ThrottleTurn, B->ThrottleTurn, Pct);
		PitchInput = FMath::Lerp(A->PitchInput, B->PitchInput, Pct);
		bBoostHeld = Pct < 0.5f ? A->bBoostHeld : B->bBoostHeld;
		bGlideHeld = Pct < 0.5f ? A->bGlideHeld : B->bGlideHeld;
		bTakeOffPressed = B->bTakeOffPressed;
		bLandPressed = B->bLandPressed;
		FlightActionSeq = B->FlightActionSeq;
		FlightStreamingSpeedScale = B->FlightStreamingSpeedScale;
	}
};

/**
 * Rollback-owned mount simulation state replicated in FMoverSyncState.
 */
USTRUCT(BlueprintType)
struct PROJECT_JMOUNT_API FProject_JMountMoverSyncState : public FMoverDataStructBase
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	EProject_JMountFlightState FlightState = EProject_JMountFlightState::Grounded;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	bool bIsGliding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	float AirSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	float PitchDeg = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	float BankDeg = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount|Mover")
	float Stamina = 100.0f;

	virtual FMoverDataStructBase* Clone() const override { return new FProject_JMountMoverSyncState(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		uint8 StateByte = static_cast<uint8>(FlightState);
		Ar << StateByte << bIsGliding << AirSpeed << PitchDeg << BankDeg << Stamina;
		if (Ar.IsLoading())
		{
			FlightState = static_cast<EProject_JMountFlightState>(StateByte);
		}
		bOutSuccess = true;
		return true;
	}
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		const auto* Other = static_cast<const FProject_JMountMoverSyncState*>(&AuthorityState);
		return FlightState != Other->FlightState || bIsGliding != Other->bIsGliding
			|| !FMath::IsNearlyEqual(AirSpeed, Other->AirSpeed, 15.0f)
			|| !FMath::IsNearlyEqual(PitchDeg, Other->PitchDeg, 2.0f)
			|| !FMath::IsNearlyEqual(BankDeg, Other->BankDeg, 2.0f)
			|| !FMath::IsNearlyEqual(Stamina, Other->Stamina, 2.0f);
	}
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override
	{
		const auto* A = static_cast<const FProject_JMountMoverSyncState*>(&From);
		const auto* B = static_cast<const FProject_JMountMoverSyncState*>(&To);
		FlightState = Pct < 0.5f ? A->FlightState : B->FlightState;
		bIsGliding = Pct < 0.5f ? A->bIsGliding : B->bIsGliding;
		AirSpeed = FMath::Lerp(A->AirSpeed, B->AirSpeed, Pct);
		PitchDeg = FMath::Lerp(A->PitchDeg, B->PitchDeg, Pct);
		BankDeg = FMath::Lerp(A->BankDeg, B->BankDeg, Pct);
		Stamina = FMath::Lerp(A->Stamina, B->Stamina, Pct);
	}
};

/** Shared tuning profile for ground and 3D energy-flying Mover mounts (BP03). */
USTRUCT(BlueprintType)
struct PROJECT_JMOUNT_API FProject_JMountMotionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "100.0"))
	float GroundWalkSpeed = 550.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "100.0"))
	float GroundGallopSpeed = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "10.0"))
	float TurnRateAtRestDegPerSec = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "10.0"))
	float TurnRateAtGallopDegPerSec = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight")
	bool bCanFly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "200.0"))
	float FlightStallSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "400.0"))
	float FlightCruiseSpeed = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "600.0"))
	float FlightDiveMaxSpeed = 3400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "50.0"))
	float GlideSinkRateCmPerSec = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "10.0"))
	float MaxStamina = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float ClimbStaminaCostPerSec = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float BoostStaminaCostPerSec = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float GlideStaminaRegenPerSec = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0.0"))
	float GroundStaminaRegenPerSec = 25.0f;
};

namespace Project_J::Mount
{
	/**
	 * Pure deterministic 3D energy-flight solver shared by UProject_JFlyingMountMode and automation tests.
	 * Diving gains speed up to FlightDiveMaxSpeed at zero stamina cost; climbing spends stamina and
	 * clamps above FlightStallSpeed; exhausting stamina automatically transitions to gliding.
	 */
	PROJECT_JMOUNT_API void StepFlyingEnergyModel(
		const FProject_JMountMoverInputs& Inputs,
		const FProject_JMountMotionProfile& Profile,
		float DeltaSeconds,
		FProject_JMountMoverSyncState& InOutState);
}
