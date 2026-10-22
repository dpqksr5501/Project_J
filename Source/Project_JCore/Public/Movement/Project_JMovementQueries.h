#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class UCharacterMoverComponent;
class USkeletalMeshComponent;
class UCapsuleComponent;

namespace Project_J::Movement
{
/** Game-thread observation of the active physical backend, never an animation-selected state. */
struct FPhysicalFrame
{
	bool bValid = false;
	bool bMover = false;
	bool bGrounded = false;
	bool bInAir = false;
 bool bHasRootMotion = false;
 bool bUsesControlFacing = false;
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	FVector OrientationIntent = FVector::ZeroVector;
	float MaxSpeed = 0.f;
	float MaxAcceleration = 0.f;
	float MaxDeceleration = 0.f;
	float GroundFriction = 0.f;
	float MaxStepHeight = 0.f;
	EMovementMode Mode = MOVE_None;
	uint8 CustomMode = 0;
	FHitResult Floor;
};

PROJECT_JCORE_API UCharacterMoverComponent* FindMover(const AActor* Owner);
PROJECT_JCORE_API FPhysicalFrame ReadPhysicalFrame(const AActor* Owner);
PROJECT_JCORE_API bool IsGrounded(const AActor* Owner);
PROJECT_JCORE_API bool IsInAir(const AActor* Owner);
PROJECT_JCORE_API USkeletalMeshComponent* FindPoseSource(const AActor* Owner);
PROJECT_JCORE_API UCapsuleComponent* FindCapsule(const AActor* Owner);
PROJECT_JCORE_API bool SetMovementMode(AActor* Owner, EMovementMode Mode, uint8 CustomMode=0);
PROJECT_JCORE_API bool IsWalkable(const AActor* Owner, const FHitResult& Hit);
}
