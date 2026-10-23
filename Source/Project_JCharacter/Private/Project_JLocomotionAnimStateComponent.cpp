#include "Project_JLocomotionAnimStateComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

UProject_JLocomotionAnimStateComponent::UProject_JLocomotionAnimStateComponent()
{
}

void UProject_JLocomotionAnimStateComponent::ApplyTransitionPolicy(const FProject_JLocomotionTransitionPolicy& InPolicy)
{ IdleSpeedThreshold=InPolicy.IdleSpeedThreshold; }

void UProject_JLocomotionAnimStateComponent::BeginPlay()
{
	bMovementTagOwnershipEnding = false;
	// Streaming can begin the same component again after EndPlay. Its timers
	// and tag contributions were retired with the previous presentation episode.
	bIsJumping = bPredictedRemoteJumpStart = bIgnoreNextLandingForJumpStart = false;
	JumpStartElapsedTime = JumpStartTimeoutElapsedTime = 0.f;
	StopFallOffStart();
	ClearActiveLandingState();
	bIsPhysicallyInAir = bLandingIgnoresRemoteGroundSpeed = bLandingCancelEventDispatched = bRemoteMoveReleasedWhileAirborne = false;
	Super::BeginPlay();
}

void UProject_JLocomotionAnimStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bMovementTagOwnershipEnding = true;
	ClearJumpStartTimers();
	ClearFallOffStartTimers();
	ClearLandingTimers();
	ClearOwnedMovementGameplayTags();
	Super::EndPlay(EndPlayReason);
}

void UProject_JLocomotionAnimStateComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bMovementTagOwnershipEnding = true;
	ClearJumpStartTimers();
	ClearFallOffStartTimers();
	ClearLandingTimers();
	ClearOwnedMovementGameplayTags();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
 if(bMovementTagOwnershipEnding || !IsRegistered() || !FMath::IsFinite(DeltaTime) || DeltaTime<=0.f) return;
 AProject_JPlayerCharacter* PlayerOwner=nullptr;
 if(!RefreshOwnerReferencesForUpdate(PlayerOwner)) { HiddenRemoteUpdateAccumulator=0.f; return; }
 AdvanceStateClocks(DeltaTime);
 if(ShouldSkipUpdateForCurrentContext(DeltaTime)) return;
 const auto MovementSnapshot=BuildMovementSnapshot(*PlayerOwner);
 ApplyMovementSnapshot(DeltaTime,MovementSnapshot);
 const bool bPhysicalAir=IsInAirForAnimation();
 if(bUsingLocalInputState) UpdateLocalAirState(bPhysicalAir);
 else UpdateRemoteAirState(DeltaTime,IsRemoteInAirForAnimation(bPhysicalAir),bPhysicalAir);
 RefreshMovementInputState(DeltaTime,GetMovementInputForState());
 bUseSprintLocomotion=(bHasMoveInput || GroundSpeed>IdleSpeedThreshold) && bWantsSprint;
}


bool UProject_JLocomotionAnimStateComponent::RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner)
{
	if (!GetPlayerOwner())
	{
		CacheOwnerReferences();
	}

	OutPlayerOwner = GetPlayerOwner();
	return OutPlayerOwner != nullptr;
}

bool UProject_JLocomotionAnimStateComponent::ShouldSkipUpdateForCurrentContext(float& DeltaTime)
{
	bUsingLocalInputState = ShouldUseLocalInputState();
	bDedicatedServerContext = IsDedicatedServerContext();
	if (bDedicatedServerContext && bSkipDedicatedServerAnimStateUpdate)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return true;
	}

	HiddenRemoteUpdateAccumulator += DeltaTime;
	bRecentlyRendered = WasRecentlyRendered(RecentlyRenderedTolerance);
	if (!bUsingLocalInputState && !bRecentlyRendered)
	{
		const float UpdateInterval = HiddenRemoteUpdateInterval;
		if (UpdateInterval > 0.0f)
		{
			if (HiddenRemoteUpdateAccumulator < UpdateInterval)
			{
				return true;
			}
		}
	}

	// Include skipped frames when sampling velocity and updating movement context,
	// also when visibility or the configured interval changes between samples.
	DeltaTime = HiddenRemoteUpdateAccumulator;
	HiddenRemoteUpdateAccumulator = 0.0f;
	return false;
}



bool UProject_JLocomotionAnimStateComponent::ShouldUseLocalInputState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return PlayerOwner && (PlayerOwner->IsLocallyControlled() || bUseInputDerivedRequestsForRemotePlayers);
}

bool UProject_JLocomotionAnimStateComponent::IsSprintRequestedForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return (bSprintInputHeld && (!PlayerOwner || PlayerOwner->IsSprintInputDirectionAllowed())) ||
		(PlayerOwner && PlayerOwner->IsSprintLocomotionAllowed());
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteInAirForAnimation(bool bMovementReportsInAir) const
{
	if (!bMovementReportsInAir)
	{
		return false;
	}

	if (FMath::Abs(VerticalSpeed) <= RemoteGroundedVerticalSpeedTolerance && IsRemoteGroundedByProbe())
	{
		return false;
	}

	return true;
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteGroundedByProbe() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const UWorld* World = GetWorld();
	const UCapsuleComponent* CapsuleComponent = GetCachedCapsuleComponent();
	if (!bUseRemoteGroundProbe || !PlayerOwner || !World || !CapsuleComponent)
	{
		return false;
	}

	const FVector Start = PlayerOwner->GetActorLocation();
	const float TraceDistance = CapsuleComponent->GetScaledCapsuleHalfHeight() + RemoteGroundProbeDistance;
	const FVector End = Start - FVector(0.0f, 0.0f, TraceDistance);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RemoteGroundProbe), false, PlayerOwner);
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit;
}

FVector2D UProject_JLocomotionAnimStateComponent::GetMovementInputForState() const
{
	return ShouldUseLocalInputState() ? GetLocalMovementInputForState() : GetRemoteMovementInputForState();
}

FVector2D UProject_JLocomotionAnimStateComponent::GetLocalMovementInputForState() const
{
	return CachedMoveInput.GetClampedToMaxSize(1.0f);
}

FVector2D UProject_JLocomotionAnimStateComponent::GetRemoteMovementInputForState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return FVector2D::ZeroVector;
	}

	FVector HorizontalVelocity = PlayerOwner->GetVelocity();
	HorizontalVelocity.Z = 0.0f;
	if (HorizontalVelocity.SizeSquared() <= FMath::Square(RemoteMoveSpeedThreshold))
	{
		return FVector2D::ZeroVector;
	}

	const FVector LocalVelocity = PlayerOwner->GetActorTransform().InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
	return FVector2D(LocalVelocity.Y, LocalVelocity.X).GetClampedToMaxSize(1.0f);
}

FProject_JLocomotionRuntimeSnapshot UProject_JLocomotionAnimStateComponent::BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const
{
	FProject_JLocomotionRuntimeSnapshot Snapshot;
	Snapshot.Velocity = PlayerOwner.GetVelocity();
	Snapshot.HorizontalVelocity = FVector(Snapshot.Velocity.X, Snapshot.Velocity.Y, 0.0f);
	Snapshot.VerticalSpeed = Snapshot.Velocity.Z;
	Snapshot.GroundSpeed = Snapshot.HorizontalVelocity.Size();
	Snapshot.bWantsSprint = IsSprintRequestedForAnimation();
	Snapshot.bHasSprintMovementIntent = CachedMoveInput.Size() > MoveInputDeadZone || Snapshot.GroundSpeed > IdleSpeedThreshold;
	return Snapshot;
}

void UProject_JLocomotionAnimStateComponent::ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot)
{
	VerticalSpeed = Snapshot.VerticalSpeed;
	GroundSpeed = Snapshot.GroundSpeed;
	bWantsSprint = Snapshot.bWantsSprint;
	bUseSprintLocomotion =
		(bHasMoveInput || GroundSpeed>IdleSpeedThreshold) &&
		bWantsSprint &&
		Snapshot.bHasSprintMovementIntent;
}

void UProject_JLocomotionAnimStateComponent::AdvanceStateClocks(float DeltaTime)
{

	if (bIsJumping)
	{
		JumpStartElapsedTime += DeltaTime;
	}

	if (bIsLanding)
	{
		LandingElapsedTime += DeltaTime;
		bCanExitLanding = LandingElapsedTime >= LandingMinHoldTime;
	}
}

void UProject_JLocomotionAnimStateComponent::RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput)
{
 bPrevHasMoveInput=bResolvedMoveInputLastUpdate;
 MoveInputSize=MoveInput.Size();
 bHasMoveInput=MoveInputSize>MoveInputDeadZone;
 MoveInputHeldTime=bHasMoveInput?MoveInputHeldTime+DeltaTime:0.f;
 bResolvedMoveInputLastUpdate=bHasMoveInput;

}
