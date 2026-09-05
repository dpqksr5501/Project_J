#include "Components/Project_JReplicatedAnimEventComponent.h"

#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JPlayerCharacter.h"

namespace
{
float GetServerWorldTimeSeconds(const UWorld* World)
{
	if (!World)
	{
		return 0.0f;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}
}

UProject_JReplicatedAnimEventComponent::UProject_JReplicatedAnimEventComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JReplicatedAnimEventComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JReplicatedAnimEventComponent, ReplicatedAnimEvents, COND_SkipOwner);
}

void UProject_JReplicatedAnimEventComponent::Initialize(
	UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent,
	UProject_JAnimationUpdateCoordinatorComponent* InAnimationUpdateCoordinator)
{
	LocomotionAnimStateComponent = InLocomotionAnimStateComponent;
	AnimationUpdateCoordinator = InAnimationUpdateCoordinator;
}

void UProject_JReplicatedAnimEventComponent::DispatchMoveStarted(bool bWasSprintingForStart)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		ApplyMoveEvent(true, bWasSprintingForStart);
		ReplicateLatestState();
		return;
	}
	ServerDispatchMoveState(true, bWasSprintingForStart);
}

void UProject_JReplicatedAnimEventComponent::DispatchMoveStopped(bool bWasSprintingAtStop)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		ApplyMoveEvent(false, bWasSprintingAtStop);
		ReplicateLatestState();
		return;
	}
	ServerDispatchMoveState(false, bWasSprintingAtStop);
}

void UProject_JReplicatedAnimEventComponent::DispatchFallOffStarted()
{
	DispatchEvent(EProject_JReplicatedAnimEventType::FallOffStart);
}

void UProject_JReplicatedAnimEventComponent::DispatchLandingStarted(
	float ImpactSpeed, bool bWasMoving, bool bWasSprinting, bool bWasHeavy)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	ApplyLandingStart(ImpactSpeed, bWasMoving, bWasSprinting, bWasHeavy);
	ReplicateLatestState();
}

void UProject_JReplicatedAnimEventComponent::DispatchLandingCancelled()
{
	DispatchEvent(EProject_JReplicatedAnimEventType::LandingCancel);
}

void UProject_JReplicatedAnimEventComponent::DispatchTurnInPlaceStarted(uint8 DirectionBucket, float TargetFacingYaw)
{
	AActor* Owner = GetOwner();
	if (!Owner || DirectionBucket < 1 || DirectionBucket > 4 || !FMath::IsFinite(TargetFacingYaw))
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (CanServerAcceptTurnInPlace(DirectionBucket, TargetFacingYaw))
		{
			ApplyTurnInPlace(DirectionBucket, TargetFacingYaw);
			ReplicateLatestState();
		}
		return;
	}

	ServerDispatchTurnInPlace(DirectionBucket, TargetFacingYaw);
}

void UProject_JReplicatedAnimEventComponent::DispatchEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		ApplyEvent(EventType, bFlag);
		ReplicateLatestState();
		return;
	}
	ServerDispatchEvent(EventType, bFlag);
}

void UProject_JReplicatedAnimEventComponent::ServerDispatchMoveState_Implementation(
	bool bIsMoving, bool bWasSprintingAtBoundary)
{
	ApplyMoveEvent(bIsMoving, bWasSprintingAtBoundary);
	ReplicateLatestState();
}

void UProject_JReplicatedAnimEventComponent::ServerDispatchEvent_Implementation(
	EProject_JReplicatedAnimEventType EventType, bool bFlag)
{
	if (EventType == EProject_JReplicatedAnimEventType::MoveStart ||
		EventType == EProject_JReplicatedAnimEventType::MoveStop ||
		EventType == EProject_JReplicatedAnimEventType::LandingStart)
	{
		return;
	}
	ApplyEvent(EventType, bFlag);
	ReplicateLatestState();
}

void UProject_JReplicatedAnimEventComponent::ServerDispatchTurnInPlace_Implementation(uint8 DirectionBucket, float TargetFacingYaw)
{
	if (!CanServerAcceptTurnInPlace(DirectionBucket, TargetFacingYaw))
	{
		return;
	}

	ApplyTurnInPlace(DirectionBucket, TargetFacingYaw);
	ReplicateLatestState();
}

void UProject_JReplicatedAnimEventComponent::ApplyEvent(EProject_JReplicatedAnimEventType EventType, bool bFlag)
{
	switch (EventType)
	{
	case EProject_JReplicatedAnimEventType::MoveStart:
		ApplyMoveEvent(true, bFlag);
		break;
	case EProject_JReplicatedAnimEventType::MoveStop:
		ApplyMoveEvent(false, bFlag);
		break;
	case EProject_JReplicatedAnimEventType::FallOffStart:
		++ReplicatedAnimEvents.FallOffStartCounter;
		ReplicatedAnimEvents.FallOffEventOrder = ++NextSemanticEventOrder;
		break;
	case EProject_JReplicatedAnimEventType::LandingCancel:
		ApplyLandingCancel();
		break;
	case EProject_JReplicatedAnimEventType::LandingStart:
	case EProject_JReplicatedAnimEventType::TurnInPlace:
	default:
		break;
	}
}

void UProject_JReplicatedAnimEventComponent::ApplyTurnInPlace(uint8 DirectionBucket, float TargetFacingYaw)
{
	++ReplicatedAnimEvents.TurnInPlaceSequence;
	ReplicatedAnimEvents.TurnInPlaceEventOrder = ++NextSemanticEventOrder;
	ReplicatedAnimEvents.TurnInPlaceServerTimeSeconds = GetServerWorldTimeSeconds(GetWorld());
	ReplicatedAnimEvents.TurnInPlaceDirectionBucket = DirectionBucket;
	ReplicatedAnimEvents.TurnInPlaceTargetFacingYaw = FRotator::NormalizeAxis(TargetFacingYaw);
}

bool UProject_JReplicatedAnimEventComponent::CanServerAcceptTurnInPlace(uint8 DirectionBucket, float TargetFacingYaw) const
{
	if (DirectionBucket < 1 || DirectionBucket > 4 || !FMath::IsFinite(TargetFacingYaw))
	{
		return false;
	}

	const AProject_JPlayerCharacter* PlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!PlayerOwner || !PlayerOwner->IsCombatModeActive())
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = PlayerOwner->GetCharacterMovement();
	return Movement && Movement->IsMovingOnGround() && PlayerOwner->GetVelocity().Size2D() <= 20.0f;
}

void UProject_JReplicatedAnimEventComponent::ApplyMoveEvent(bool bIsMoving, bool bWasSprintingAtBoundary)
{
	ReplicatedAnimEvents.bIsMoving = bIsMoving;
	// This captures gait at the edge; it is not a continuously replicated sprint flag.
	ReplicatedAnimEvents.bIsSprinting = bWasSprintingAtBoundary;
	ReplicatedAnimEvents.MoveServerTimeSeconds = GetServerWorldTimeSeconds(GetWorld());
	ReplicatedAnimEvents.MoveEventOrder = ++NextSemanticEventOrder;
	++ReplicatedAnimEvents.MoveSequence;
}

void UProject_JReplicatedAnimEventComponent::ApplyLandingStart(
	float ImpactSpeed, bool bWasMoving, bool bWasSprinting, bool bWasHeavy)
{
	++ReplicatedAnimEvents.LandingSequence;
	++ReplicatedAnimEvents.LandingRevision;
	ReplicatedAnimEvents.LandingEventOrder = ++NextSemanticEventOrder;
	ReplicatedAnimEvents.LandingServerTimeSeconds = GetServerWorldTimeSeconds(GetWorld());
	ReplicatedAnimEvents.LandingImpactSpeed = FMath::Max(ImpactSpeed, 0.0f);
	ReplicatedAnimEvents.bLandingActive = true;
	ReplicatedAnimEvents.bLandingWasMoving = bWasMoving;
	ReplicatedAnimEvents.bLandingWasSprinting = bWasMoving && bWasSprinting;
	ReplicatedAnimEvents.bLandingWasHeavy = bWasHeavy;
}

void UProject_JReplicatedAnimEventComponent::ApplyLandingCancel()
{
	++ReplicatedAnimEvents.LandingRevision;
	++ReplicatedAnimEvents.LandingCancelCounter;
	ReplicatedAnimEvents.LandingEventOrder = ++NextSemanticEventOrder;
	ReplicatedAnimEvents.LandingServerTimeSeconds = GetServerWorldTimeSeconds(GetWorld());
	ReplicatedAnimEvents.bLandingActive = false;
}

void UProject_JReplicatedAnimEventComponent::ReplicateLatestState()
{
	if (AActor* Owner = GetOwner())
	{
		MulticastAnimEventState(ReplicatedAnimEvents);
		Owner->ForceNetUpdate();
	}
}

void UProject_JReplicatedAnimEventComponent::MulticastAnimEventState_Implementation(
	FProject_JReplicatedAnimEventState EventState)
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const APawn* PawnOwner = Cast<APawn>(Owner);
	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		return;
	}
	ApplyReplicatedEvents(EventState, FProject_JReplicatedAnimEventState());
}

void UProject_JReplicatedAnimEventComponent::OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState)
{
	ApplyReplicatedEvents(ReplicatedAnimEvents, PreviousState);
}

void UProject_JReplicatedAnimEventComponent::ApplyReplicatedEvents(
	const FProject_JReplicatedAnimEventState& CurrentState,
	const FProject_JReplicatedAnimEventState& PreviousState)
{
	if (!LocomotionAnimStateComponent)
	{
		return;
	}

	bool bMovePending = CurrentState.MoveSequence > LastAppliedMoveSequence;
	bool bLandingPending = CurrentState.LandingRevision > LastAppliedLandingRevision;
	bool bFallOffPending = CurrentState.FallOffStartCounter > LastAppliedFallOffCounter;
	bool bTurnInPlacePending = CurrentState.TurnInPlaceSequence > LastAppliedTurnInPlaceSequence;
	(void)PreviousState;

	// Apply coalesced state in the authoritative server order. This prevents an old
	// MoveStart from cancelling a newer landing when both arrive in one net update.
	while (bMovePending || bLandingPending || bFallOffPending || bTurnInPlacePending)
	{
		const int32 MoveOrder = bMovePending ? CurrentState.MoveEventOrder : MAX_int32;
		const int32 LandingOrder = bLandingPending ? CurrentState.LandingEventOrder : MAX_int32;
		const int32 FallOffOrder = bFallOffPending ? CurrentState.FallOffEventOrder : MAX_int32;
		const int32 TurnInPlaceOrder = bTurnInPlacePending ? CurrentState.TurnInPlaceEventOrder : MAX_int32;
		if (MoveOrder <= LandingOrder && MoveOrder <= FallOffOrder && MoveOrder <= TurnInPlaceOrder)
		{
			ApplyRemoteMoveState(CurrentState);
			bMovePending = false;
		}
		else if (LandingOrder <= FallOffOrder && LandingOrder <= TurnInPlaceOrder)
		{
			ApplyRemoteLandingState(CurrentState);
			bLandingPending = false;
		}
		else if (FallOffOrder <= TurnInPlaceOrder)
		{
			ApplyRemoteFallOffState(CurrentState);
			bFallOffPending = false;
		}
		else
		{
			ApplyRemoteTurnInPlaceState(CurrentState);
			bTurnInPlacePending = false;
		}
	}
}

void UProject_JReplicatedAnimEventComponent::ApplyRemoteMoveState(const FProject_JReplicatedAnimEventState& State)
{
	LastAppliedMoveSequence = State.MoveSequence;
	if (State.MoveEventOrder <= LastAppliedSemanticEventOrder)
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("RemoteAnimSemanticDrop Actor=%s Type=Move Order=%d LastOrder=%d MoveSeq=%d"),
				*GetNameSafe(GetOwner()),
				State.MoveEventOrder, LastAppliedSemanticEventOrder, State.MoveSequence);
		}
		return;
	}
	LastAppliedSemanticEventOrder = State.MoveEventOrder;
	RequestUrgentRemoteAnimationUpdate();
	if (State.bIsMoving)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStarted(State.bIsSprinting);
	}
	else
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStopped(State.bIsSprinting);
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("RemoteAnimSemantic Actor=%s Type=%s Order=%d MoveSeq=%d SprintAtEdge=%s Age=%.3f"),
			*GetNameSafe(GetOwner()),
			State.bIsMoving ? TEXT("MoveStart") : TEXT("MoveStop"), State.MoveEventOrder,
			State.MoveSequence, State.bIsSprinting ? TEXT("true") : TEXT("false"),
			ResolveServerEventAgeSeconds(State.MoveServerTimeSeconds));
	}
}

void UProject_JReplicatedAnimEventComponent::ApplyRemoteLandingState(const FProject_JReplicatedAnimEventState& State)
{
	LastAppliedLandingRevision = State.LandingRevision;
	if (State.LandingEventOrder <= LastAppliedSemanticEventOrder)
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("RemoteAnimSemanticDrop Actor=%s Type=Landing Order=%d LastOrder=%d LandSeq=%d LandRev=%d"),
				*GetNameSafe(GetOwner()),
				State.LandingEventOrder, LastAppliedSemanticEventOrder,
				State.LandingSequence, State.LandingRevision);
		}
		return;
	}
	LastAppliedSemanticEventOrder = State.LandingEventOrder;
	RequestUrgentRemoteAnimationUpdate();
	const float EventAge = ResolveServerEventAgeSeconds(State.LandingServerTimeSeconds);
	if (State.bLandingActive)
	{
		LocomotionAnimStateComponent->HandleReplicatedLandingStarted(
			State.LandingSequence, EventAge, State.LandingImpactSpeed,
			State.bLandingWasMoving, State.bLandingWasSprinting, State.bLandingWasHeavy);
	}
	else
	{
		LocomotionAnimStateComponent->HandleReplicatedLandingCancelled(State.LandingSequence);
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("RemoteAnimSemantic Actor=%s Type=%s Order=%d LandSeq=%d LandRev=%d Moving=%s Sprint=%s Heavy=%s Impact=%.1f Age=%.3f"),
			*GetNameSafe(GetOwner()),
			State.bLandingActive ? TEXT("LandingStart") : TEXT("LandingCancel"),
			State.LandingEventOrder, State.LandingSequence, State.LandingRevision,
			State.bLandingWasMoving ? TEXT("true") : TEXT("false"),
			State.bLandingWasSprinting ? TEXT("true") : TEXT("false"),
			State.bLandingWasHeavy ? TEXT("true") : TEXT("false"),
			State.LandingImpactSpeed, EventAge);
	}
}

void UProject_JReplicatedAnimEventComponent::ApplyRemoteFallOffState(const FProject_JReplicatedAnimEventState& State)
{
	LastAppliedFallOffCounter = State.FallOffStartCounter;
	if (State.FallOffEventOrder <= LastAppliedSemanticEventOrder)
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("RemoteAnimSemanticDrop Actor=%s Type=FallOff Order=%d LastOrder=%d Counter=%d"),
				*GetNameSafe(GetOwner()),
				State.FallOffEventOrder, LastAppliedSemanticEventOrder, State.FallOffStartCounter);
		}
		return;
	}
	LastAppliedSemanticEventOrder = State.FallOffEventOrder;
	RequestUrgentRemoteAnimationUpdate();
	LocomotionAnimStateComponent->HandleReplicatedFallOffStarted();
}

void UProject_JReplicatedAnimEventComponent::ApplyRemoteTurnInPlaceState(const FProject_JReplicatedAnimEventState& State)
{
	LastAppliedTurnInPlaceSequence = State.TurnInPlaceSequence;
	if (State.TurnInPlaceEventOrder <= LastAppliedSemanticEventOrder)
	{
		return;
	}

	LastAppliedSemanticEventOrder = State.TurnInPlaceEventOrder;
	RequestUrgentRemoteAnimationUpdate();
	LocomotionAnimStateComponent->HandleReplicatedTurnInPlaceStarted(
		State.TurnInPlaceSequence,
		ResolveServerEventAgeSeconds(State.TurnInPlaceServerTimeSeconds),
		State.TurnInPlaceDirectionBucket,
		State.TurnInPlaceTargetFacingYaw);

	// A remote TIP is most usefully diagnosed alongside the local TIP state/asset
	// telemetry. Do not require the unrelated generic transition trace CVar.
	if (Project_J::MotionMatchingCVars::GetTurnInPlaceDebugMode() > 0)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("RemoteAnimSemantic Actor=%s Type=TurnInPlace Order=%d Seq=%d Bucket=%d TargetYaw=%.1f Age=%.3f"),
			*GetNameSafe(GetOwner()), State.TurnInPlaceEventOrder, State.TurnInPlaceSequence,
			State.TurnInPlaceDirectionBucket, State.TurnInPlaceTargetFacingYaw,
			ResolveServerEventAgeSeconds(State.TurnInPlaceServerTimeSeconds));
	}
}

float UProject_JReplicatedAnimEventComponent::ResolveServerEventAgeSeconds(float ServerTimeSeconds) const
{
	return ServerTimeSeconds > 0.0f
		? FMath::Max(0.0f, GetServerWorldTimeSeconds(GetWorld()) - ServerTimeSeconds)
		: 0.0f;
}

void UProject_JReplicatedAnimEventComponent::RequestUrgentRemoteAnimationUpdate() const
{
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetOwner());
	if (!PlayerCharacter)
	{
		return;
	}
	if (!AnimationUpdateCoordinator)
	{
		return;
	}
	const UProject_JLocomotionProfile* Profile = PlayerCharacter->GetLocomotionProfile();
	const float Duration = Profile
		? Profile->RemoteVisualPolicy.UrgentOneShotAnimationUpdateDuration
		: 0.10f;
	AnimationUpdateCoordinator->RequestUrgentRemoteAnimationUpdate(Duration);
}
