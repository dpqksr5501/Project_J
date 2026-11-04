#pragma once

#include "CoreMinimal.h"

class AProject_JPlayerState;
struct FProject_JLocalProfileSnapshot;
struct FProject_JItemInstanceData;
struct FProject_JEquipmentArrayItem;

/** GT-only canonical value capture/projection shared by durable and native reward callers.
 * No I/O, live installer, notifications, definition loading or worker UObject access. */
namespace ProjectJ::OwnedItems
{
PROJECT_J_API bool CaptureItems(AProject_JPlayerState* Player, FProject_JLocalProfileSnapshot& Snapshot,
    TSet<FGuid>* Locked = nullptr);

/** Existing durable restore policy, including equipped-only persistent locks. */
PROJECT_J_API bool ResolveItems(AProject_JPlayerState* Player, const FProject_JLocalProfileSnapshot& Snapshot,
    TArray<FProject_JItemInstanceData>& Bag, TArray<FProject_JEquipmentArrayItem>& Slots,
    bool bTrustedApply = false, FString* Failure = nullptr);

/** Native grant policy: preserve exact Before flags on surviving IDs; new items are unlocked.
 * Caller still owns the source/ASC epoch, baseline preflight and all-install-before-notify boundary. */
PROJECT_J_API bool ResolveNativeGrant(AProject_JPlayerState* Player, const FProject_JLocalProfileSnapshot& Snapshot,
    const TArray<FProject_JItemInstanceData>& BeforeBag, TArray<FProject_JItemInstanceData>& Bag,
    TArray<FProject_JEquipmentArrayItem>& Slots, FString* Failure = nullptr);
}
