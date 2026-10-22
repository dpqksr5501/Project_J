#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Movement/Project_JMoverComponent.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "Movement/Project_JMoverPolicy.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"

namespace Project_J::Movement
{
UCharacterMoverComponent* FindMover(const AActor* Owner)
{
	return Owner ? Owner->FindComponentByClass<UCharacterMoverComponent>() : nullptr;
}

FPhysicalFrame ReadPhysicalFrame(const AActor* Owner)
{
	check(IsInGameThread());
	FPhysicalFrame Frame;
	if (const auto* Mover = FindMover(Owner))
	{
		Frame.bValid = Frame.bMover = true;
		Frame.Velocity = Mover->GetVelocity();
		Frame.bGrounded = Mover->IsOnGround();
		Frame.bInAir = Mover->IsAirborne();
  Frame.bHasRootMotion = Mover->HasFeaturesWithTag(Mover_AnimRootMotion_Montage,false);
  const auto* Life=Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
  Frame.bUsesControlFacing=Life && Life->bStrafe;
		Frame.Mode = Frame.bGrounded ? MOVE_Walking : Mover->IsFalling() ? MOVE_Falling : Mover->IsFlying() ? MOVE_Flying : MOVE_None;
		Mover->TryGetFloorCheckHitResult(Frame.Floor);
		const auto* Input = Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>();
		if (Input) Frame.OrientationIntent = Input->GetOrientationIntentDir_WorldSpace();
		if (const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>())
		{
			Frame.MaxSpeed = Settings->MaxSpeed;
			Frame.MaxAcceleration = Settings->Acceleration;
			Frame.MaxDeceleration = Settings->Deceleration;
			Frame.GroundFriction = Settings->GroundFriction;
			Frame.MaxStepHeight = Settings->MaxStepHeight;
			if (Input) Frame.Acceleration = Input->GetMoveInput_WorldSpace().GetSafeNormal() * Settings->Acceleration;
		}
	}
	else if (const auto* Character = Cast<ACharacter>(Owner))
	{
		if (const auto* CMC = Character->GetCharacterMovement())
		{
			Frame.bValid = true;
			Frame.Velocity = CMC->Velocity;
			Frame.Acceleration = CMC->GetCurrentAcceleration();
			Frame.bGrounded = CMC->IsMovingOnGround();
			Frame.bInAir = CMC->IsFalling();
   Frame.bHasRootMotion=CMC->HasAnimRootMotion() || CMC->CurrentRootMotion.HasActiveRootMotionSources();
   Frame.bUsesControlFacing=CMC->bUseControllerDesiredRotation;
			Frame.MaxSpeed = CMC->GetMaxSpeed();
			Frame.MaxAcceleration = CMC->GetMaxAcceleration();
			Frame.MaxDeceleration = CMC->GetMaxBrakingDeceleration();
			Frame.GroundFriction = CMC->GroundFriction;
			Frame.MaxStepHeight = CMC->MaxStepHeight;
			Frame.Mode = CMC->MovementMode;
			Frame.CustomMode = CMC->CustomMovementMode;
			Frame.Floor = CMC->CurrentFloor.HitResult;
		}
	}
	return Frame;
}

bool IsGrounded(const AActor* Owner) { return ReadPhysicalFrame(Owner).bGrounded; }
bool IsInAir(const AActor* Owner) { return ReadPhysicalFrame(Owner).bInAir; }
USkeletalMeshComponent* FindPoseSource(const AActor* Owner)
{
	if (const auto* Character = Cast<ACharacter>(Owner)) return Character->GetMesh();
	if (!Owner) return nullptr;
	USkeletalMeshComponent* Source = nullptr;
	for (UActorComponent* Component : Owner->GetComponents())
		if (auto* Mesh = Cast<USkeletalMeshComponent>(Component); Mesh && Mesh->ComponentTags.Contains(TEXT("ProjectJ.PoseSource")))
		{
			if (Source) return nullptr;
			Source = Mesh;
		}
	return Source;
}
UCapsuleComponent* FindCapsule(const AActor* Owner) { return Owner ? Cast<UCapsuleComponent>(Owner->GetRootComponent()) : nullptr; }
bool SetMovementMode(AActor* Owner, EMovementMode Mode, uint8 CustomMode)
{
	if (auto* Mover = Cast<UProject_JMoverComponent>(FindMover(Owner))) { Mover->SetGameplayMovementMode(Mode); return true; }
	if (const auto* Character = Cast<ACharacter>(Owner))
		if (auto* CMC = Character->GetCharacterMovement()) { CMC->SetMovementMode(Mode, CustomMode); return true; }
	return false;
}
bool IsWalkable(const AActor* Owner, const FHitResult& Hit)
{
	if (const auto* Mover = FindMover(Owner))
		if (const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>())
			return UFloorQueryUtils::IsHitSurfaceWalkable(Hit, Mover->GetUpDirection(), Settings->MaxWalkSlopeCosine);
	if (const auto* Character = Cast<ACharacter>(Owner))
		if (const auto* CMC = Character->GetCharacterMovement()) return CMC->IsWalkable(Hit);
	return false;
}
}
