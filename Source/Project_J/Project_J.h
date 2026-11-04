// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogProject_J, Log, All);

namespace ProjectJ::Network
{
    // Realtime RPC/replication wire contract. Bump for incompatible changes;
    // independent of marketing, persistence schema and backend API versions.
    //13: typed predicted melee hit with execution-clock and node-visit context.
    inline constexpr uint32 RealtimeProtocolVersion = 13;
}
