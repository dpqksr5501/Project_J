#include "Animation/Project_JWingerAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "Movement/Project_JMoverPawn.h"

void UProject_JWingerAnimInstance::ResetWingMotion()
{
    WingMotionIndex = static_cast<int32>(EWingMotion::Idle);
    WingSprintBlendPercent = 0.f;
    GroundMotion = EWingMotion::Idle;
    bWasAirborne = false;
    bHasMotionSample = false;
    AirEpisode = 0;
    PhaseRemaining = 0.f;
}

void UProject_JWingerAnimInstance::NativeInitializeAnimation()
{
    ResetWingMotion();
    Super::NativeInitializeAnimation();
}

void UProject_JWingerAnimInstance::NativeUninitializeAnimation()
{
    ResetWingMotion();
    Super::NativeUninitializeAnimation();
}

void UProject_JWingerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const auto* Pawn = Cast<AProject_JMoverPawn>(TryGetPawnOwner());
    const auto* Mover = IsValid(Pawn) ? Pawn->GetMover() : nullptr;
    const auto* Mesh = GetSkelMeshComponent();
    if (!IsValid(Mover) || Pawn->GetNetMode() == NM_DedicatedServer ||
        (Mesh && !Mesh->ShouldEvaluatePostProcessAnimBP()) ||
        !Pawn->HasActorBegunPlay() || !Mover->HasBegunPlay() || !Mover->IsRegistered())
    {
        ResetWingMotion();
        return;
    }

    const FVector Velocity = Pawn->GetVelocity();
    const auto* Lifecycle = Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
    if (Mover->IsAirborne())
    {
        if (!bWasAirborne || (Lifecycle && Lifecycle->AirSequence != AirEpisode))
        {
            const bool bJumping = Lifecycle ? Lifecycle->bJumpEpisode : Velocity.Z > 0.f;
            PhaseRemaining = bHasMotionSample && bJumping && Velocity.Z > 0.f ? TakeoffDuration : 0.f;
            AirEpisode = Lifecycle ? Lifecycle->AirSequence : 0;
        }
        else
        {
            PhaseRemaining = FMath::Max(0.f, PhaseRemaining - DeltaSeconds);
        }
        bWasAirborne = true;
        bHasMotionSample = true;
        WingMotionIndex = static_cast<int32>(PhaseRemaining > 0.f || TakeoffDuration <= 0.f ?
            EWingMotion::Takeoff : EWingMotion::Air);
        return;
    }
    if (!Mover->IsOnGround())
    {
        ResetWingMotion();
        return;
    }

    const double Speed = Velocity.Size2D();
    // Hysteresis around stopping and the walk/run boundary keeps the blend
    // stable while Mover accelerates or decelerates through either threshold.
    const double IdleThreshold = GroundMotion == EWingMotion::Idle ? 20.0 : 10.0;
    const double RunThreshold = GroundMotion == EWingMotion::Run ? 210.0 : 250.0;
    GroundMotion = Speed < IdleThreshold ? EWingMotion::Idle :
        Speed >= RunThreshold ? EWingMotion::Run : EWingMotion::Walk;

    // Use simulation-accepted sprint and its achieved speed. Obstruction,
    // denied sprint, and unrelated velocity impulses should not tuck the wings.
    float TargetBlend = 0.f;
    if (GroundMotion == EWingMotion::Run && Lifecycle && Lifecycle->bSprint)
    {
        const float RunSpeed = Pawn->GetMoverMaxSpeed(false);
        const float SprintSpeed = Pawn->GetMoverMaxSpeed(true);
        if (RunSpeed > 0.f && SprintSpeed > RunSpeed)
        {
            TargetBlend = 100.f * FMath::Clamp((static_cast<float>(Speed) - RunSpeed) / (SprintSpeed - RunSpeed), 0.f, 1.f);
        }
    }
    WingSprintBlendPercent = FMath::FInterpTo(WingSprintBlendPercent, TargetBlend, DeltaSeconds, 12.f);

    if (bWasAirborne)
    {
        bWasAirborne = false;
        // A pose that resumes after being culled must not replay an old landing.
        const bool bFreshLanding = !Lifecycle ||
            (Lifecycle->LandingSequence == AirEpisode && Lifecycle->LandingBrakingTimeMs > 0.f);
        PhaseRemaining = bFreshLanding ? LandingDuration : 0.f;
    }
    else
    {
        PhaseRemaining = FMath::Max(0.f, PhaseRemaining - DeltaSeconds);
    }
    bHasMotionSample = true;
    WingMotionIndex = static_cast<int32>(PhaseRemaining > 0.f ? EWingMotion::Land : GroundMotion);
}
