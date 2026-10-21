// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"

struct FNavLocation;
class INavigationDataInterface;
class UObject;

class NavMovementUtils
{
	public:
	
	static bool CalculateNavMeshNormal(const FNavLocation& Location, FVector& OutNormal, const INavigationDataInterface* NavData, UObject* LogOwner = nullptr);
};