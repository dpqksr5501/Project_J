#include "Movement/Project_JMontageDashMove.h"

#include "Animation/AnimMontage.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"

FProject_JMontageDashMove::FProject_JMontageDashMove()
{
    MixMode = EMoveMixMode::OverrideVelocity;
    FinishVelocitySettings.FinishVelocityMode = ELayeredMoveFinishVelocityMode::SetVelocity;
}

bool FProject_JMontageDashMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
    const UMoverComponent* Mover, UMoverBlackboard*, FProposedMove& OutMove)
{
    const auto* Montage = MontageState.Montage.Get();
    if (!Montage || TimeStep.StepMs <= UE_KINDA_SMALL_NUMBER || StartState.SyncState.MovementMode != DefaultModeNames::Walking)
    {
        DurationMs = 0.f;
        FinishVelocitySettings.SetVelocity = FVector::ZeroVector;
        return false;
    }

    const double Elapsed = FMath::Max(0., TimeStep.BaseSimTimeMs - StartSimTimeMs);
    const double End = FMath::Min(Elapsed + TimeStep.StepMs, double(DurationMs));
    const double Rate = MontageState.PlayRate * Montage->RateScale * .001;
    const float From = MontageState.StartingMontagePosition + Elapsed * Rate;
    const float To = MontageState.StartingMontagePosition + End * Rate;
    const FVector RootDelta = Montage->ExtractRootMotionFromTrackRange(From, To, FAnimExtractContext()).GetTranslation();

    OutMove = FProposedMove();
    OutMove.MixMode = MixMode;
    OutMove.LinearVelocity = TravelDirection * (RootDelta.Size2D() * 1000. / TimeStep.StepMs);
    if (const auto* Sync = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
    {
        // Face the captured camera heading in the same simulation step as travel.
        const double DeltaYaw = FMath::FindDeltaAngleDegrees(Sync->GetOrientation_WorldSpace().Yaw, TravelDirection.Rotation().Yaw);
        OutMove.AngularVelocityDegrees.Z = DeltaYaw * 1000. / TimeStep.StepMs;
    }
    MontageState.CurrentPosition = To;

    if (End >= DurationMs)
    {
        // The input in this simulation step already passed the normal gait policy.
        // Release the dash at ordinary movement speed, using the current direction.
        const auto* Input = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
        const auto* Settings = Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
        FinishVelocitySettings.SetVelocity = Input && Settings
            ? Input->GetMoveInput_WorldSpace().GetClampedToMaxSize2D(1.) * Settings->MaxSpeed
            : FVector::ZeroVector;
    }
    return true;
}

FLayeredMoveBase* FProject_JMontageDashMove::Clone() const
{
    return new FProject_JMontageDashMove(*this);
}

void FProject_JMontageDashMove::NetSerialize(FArchive& Ar)
{
    FLayeredMove_AnimRootMotion::NetSerialize(Ar);
    Ar << TravelDirection;
    // In this engine version the base serializer omits finish velocity on save.
    Ar << FinishVelocitySettings.SetVelocity;
}

UScriptStruct* FProject_JMontageDashMove::GetScriptStruct() const
{
    return StaticStruct();
}
