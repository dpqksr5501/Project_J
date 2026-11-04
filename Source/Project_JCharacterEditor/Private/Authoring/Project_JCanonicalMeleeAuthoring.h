#pragma once
#include "CoreMinimal.h"
class UProject_JAttackDefinition;
class USkeletalMesh;
struct FProject_JMeleeTrajectory;

namespace Project_J::CanonicalMeleeAuthoring
{
    // Same RAW producer used by the selected-asset operation and native integration.
    // Allows a transient attack; never edits/saves an asset, and failure leaves OutPath untouched.
    bool BuildCandidate(const UProject_JAttackDefinition& Attack, USkeletalMesh& SourceMesh,
        FProject_JMeleeTrajectory& OutPath, FString& OutFailure);

    // Editor-only source/stamp validation; does not evaluate full poses.
    bool ValidateCurrentSource(const UProject_JAttackDefinition& Attack, USkeletalMesh& Mesh,
        const FProject_JMeleeTrajectory& Path, FString& OutFailure);
}
